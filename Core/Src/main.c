/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  *
  * Date:    18-Aug-2024
  * Author:  David Waskevich
  *
  * Description: Test application for IEE FLIP 03600-20-040 Vacuum Fluorescent Display
  *
  *              Note - adapted/ported from original Sparkfun FreeSOC2 PSoC5LP/CortexM3-based
  *              Arduino-style development kit version (available in develop branch of
  *              https://github.com/dwaskevich/IEE_FLIP_Vacuum_Fluorescent_Display.git)
  *
  * Hardware:    STM32 Blue Pill (https://predictabledesigns.com/introduction-stm32-blue-pill-stm32duino/)
  *              STM32F103C8T6 CortexM3-based development kit (64K Flash/20K SRAM, 3.3V)
  *              -> note, 30k Flash used (47%), 19k SRAM used (95%)
  *
  * IDE:         STM32CubeIDE Version: 1.16.0
  *
  * Wiring:      8-bit parallel data bus to display --> PA[7:0] (BluePill P3 header, pins 12-5)
  *              /CS   - PB0  (BluePill P3 header, pin 13)
  *               A0   - PB1  (BluePill P3 header, pin 14)
  *              /WR   - PB10 (BluePill P3 header, pin 15)
  *              /RD   - PB11 ... not used (BluePill P3 header, pin 16)
  *              /TEST - PC15 (BluePill P3 header, pin 4)
  *
  * Peripheral setup:
  *              UART - USART1 ... Tx = PA9, Rx = PA10
  *              Escape sequence timeout timer - TIM1 (oneshot mode, 20 ms interrupt)
  *              Readback timer - TIM2 (oneshot mode, 20/50 ms interrupt)
  *              Switch matrix timer - TIM3 (555 us interrupt)
  *              Hardware-based delay - TIM4 (.5 us count)
  *              I2C1 - future use (CapSense buttons)
  *              Program/Debug - SWD ... PA13 (SWDIO)/PA14 (SWDCK)
  *
  * Serial terminal (TeraTerm, etc) navigation:
  *              Up/Down arrow keys - scroll back/forward to previous/next line
  *              Right/Left arrow keys - replay line, fast/normal playback speed
  *              PageUp/PageDown keys - scroll back/forward PAGE_JUMP_SIZE lines
  *              Home key - return to the most recent line
  *              End key - go directly to the oldest line/record
  *              Delete key - enter pause/single-step mode (subsequent presses single-steps readback)
  *              Insert key - dual-purpose for now ... print the deepest FIFO level so far
  *              	and resume readback (i.e. restart readback/TIM2 timer)
  *              Escape key - abandon (escape) a readback by quickly recalling the line
  *
  * 4x4 matrix button navigation:
  *             		Col0	Col1	Col2	Col3
  *             Row0	PageUp	Home	N/A		SingleStep
  *             Row1	PageDn	End		N/A		Resume
  *             Row2	N/A		N/A		N/A		ScrollUp
  *             Row3	ReadBkF	Escape	ReadBk	ScrollDn
  *
  * Usage:       #include <iee_flip_03600_20_040.h>
  *              NOTE - arbitrarily chose BELL (ctrl-G) character to reset display.  *
  *
  * Update 18-Aug-2024:
  *		- imported source/header files from PSoC version
  *
  * Update 19-Aug-2024:
  *		- completed low-level hardware drivers for STM32
  *
  * Update 20-Aug-2024:
  *		- replaced HAL_Delay with cycle count
  *
  * Update 22-Aug-2024:
  *		- added 4x4 switch matrix
  *		- imported debounce library from https://github.com/tcleg/Button_Debouncer/tree/master
  *			-> note ... debounce library is an implementation of Jack Ganssle (https://www.ganssle.com/debouncing-pt2.htm)
  *		- replaced cycle count delay with TIM4 delay function
  *
  * Update 23-Aug-2024:
  *		- modified behavior of single-step readback
  *			-> if held, single-step reads back characters at half the readback speed
  *			-> when released, readback timer is stopped and reloaded with previous readback speed value
  *			-> press_and_release behaves the same (reads back one character for each button press)
  *
  * Update 24-Aug-2024:
  *		- fine tuned TIM4 delay function
  *			-> TIM4 is on 72MHz bus
  *			-> prescaler set to 18 (well, 18 - 1) ... 4MHz counter frequency
  *			-> modified delay_us() function to account for 4MHz clock
  *		- deactivated all column drivers in default switch condition
  *
  *
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f1xx_ll_gpio.h"
#include "iee_flip_03600_20_040.h"
#include "stdio.h"
#include "string.h"
#include "stdbool.h"
#include "button_debounce.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_FIFO_SIZE          (4096u)
#define PAGE_JUMP_SIZE          (10u)
#define INITIALIZE_REPLAY   	(0xffff)

#define LED_OFF     (1u)
#define LED_ON      (0u)

/* switch matrix defines */
#define COLUMN_INACTIVE			(1u)
#define COLUMN_ACTIVE			(0u)
#define NUM_DISPLAY_COLUMNS		(9u)
#define INTRA_COLUMN_DELAY_US	(2u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
volatile uint16_t fifo_MaxLevelReached; /* debug-oriented measure of FIFO utilization */
uint8_t rxBuffer[80]; /* may not be necessary, single variable may be enough ... needs testing */
volatile uint16_t headPointer = 0, tailPointer = 0; /* FIFO head and tail pointers */
uint8_t rxFIFO[UART_FIFO_SIZE]; /* Rx FIFO ... no rollover protection, older characters overwritten if FIFO fills */
/* main loop decision flags */
volatile bool timeoutFlag = false, readBackFlag = false, singleStepFlag = false, readButtonFlag = false;
/* button processing variables */
volatile uint8_t rawButtons0 = 0, rawButtons1 = 0;
Debouncer buttons0;
Debouncer buttons1;
/* timer restore variable */
uint32_t timerRestorValue;

/* Escape sequence state machine (decodes/parses multi-character keyboard escape sequences) */
enum escSeqStates
{
    ESCAPE, /* ESC character (0x1b) detected */
    X5B,    /* looking for second escape sequence character (0x5b) */
    X7E     /* some keys (HOME, END, INSERT) generate 4-byte sequence with last character = 0x7e */
};
enum escSeqStates escSeqState = ESCAPE;

/* Switch matrix key names */
enum switchMatrix
{
	NONE		=	0x0000,
	ONE 		=	0x0001, PAGEUP = ONE,
	FOUR 		=	0x0002, PAGEDN = FOUR,
	SEVEN		=	0x0004,
	ASTERISK	=	0x0008, READBKF = ASTERISK,
	TWO			=	0x0010, GOHOME = TWO,
	FIVE		=	0x0020, GOEND = FIVE,
	EIGHT		=	0x0040,
	ZERO		=	0x0080, ESCRB = ZERO,
	THREE		=	0x0100,
	SIX			=	0x0200,
	NINE		=	0x0400,
	HASH		=	0x0800, READBK = HASH,
	SINGLE_STEP	=	0x1000,
	RESUME		=	0x2000,
	SCROLL_UP	=	0x4000,
	SCROLL_DOWN	=	0x8000
} keyNames;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */

void delay_us(uint16_t delay);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint8_t printBuffer[100]; /* used for sprintf debugging */
  uint8_t rxData;
  uint8_t entryMode, cursorPosition;
  uint16_t currentLineBufferID = 0;
  uint8_t updateDisplayFlag = FALSE;
  bool isEchoFlag = true;
  bool isEscapeSequenceFlag = false;
  char escSequence[4] = {0};
  uint8_t escSequenceNum = 0;
  bool clearDisplayFlag = false;
  static uint16_t recallLineNumber = 0;
  static uint16_t replayCharNumber = 0;
  uint8_t* str;
  uint16_t buttonsPressed = 0, previousButtonsPressed = 0;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */

  HAL_UART_Receive_IT(&huart1, rxBuffer, 1); /* start UART in interrupt mode */
  HAL_UART_Transmit(&huart1, (uint8_t *) "\x1b[2J\x1b[;HUART started\r\n", sizeof("\x1b[2J\x1b[;HUART started\r\n"), 500);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_OFF);

  /* initialize VFD display (returns entry mode defined in .h file) */
  entryMode = VFD_InitializeDisplay(DEFAULT_ENTRY_MODE);

  /* initialize display history */
  sprintf((char *) printBuffer, "Initializing display history. Number of pages = %d\r\n", VFD_InitDisplayHistory());
  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

  sprintf((char *) printBuffer, "SRAM usage for display history = %d\r\n", VFD_GetSizeOfHistoryArray());
  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

  /* clear timer interrupts before starting forever loop */
  __HAL_TIM_CLEAR_IT(&htim1, TIM_FLAG_UPDATE);
  __HAL_TIM_CLEAR_IT(&htim2, TIM_FLAG_UPDATE);
  __HAL_TIM_CLEAR_IT(&htim3, TIM_FLAG_UPDATE);

  HAL_TIM_Base_Start_IT(&htim3);
  HAL_TIM_Base_Start(&htim4);

  ButtonDebounceInit(&buttons0, 0);
  ButtonDebounceInit(&buttons1, 0);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  /* check rxFIFO for incoming characters */
	  if(tailPointer != headPointer) /* if true, new data is available */
	  {
		  rxData = rxFIFO[tailPointer++]; /* retrieve new character from FIFO */
		  if(tailPointer >= UART_FIFO_SIZE) /* manage FIFO pointer rollover */
			  tailPointer = 0;
		  isEchoFlag = true; /* set flag on each new character received (true if printable character, will be reset to false if escape sequence is detected) */
		  HAL_TIM_Base_Stop_IT(&htim1); /* stop the ESC timeout timer on each new character received */

		  /* parse incoming characters for carriage return and/or line feed */
		  if(CR == rxData || LF == rxData) /* handle CR/LF here */
		  {
			  if(CR == rxData)
				  HAL_UART_Transmit(&huart1, &rxData, 1, 100); /* echo back */

			  if(LF == rxData)
				  HAL_UART_Transmit(&huart1, &rxData, 1, 100); /* echo back */

              replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
              clearDisplayFlag = true; /* reminder to clear display on next received character (style/aesthetic choice) */
              HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_ON); /* UserLED "ON" to indicate end-of-line (display clear pending) */

              currentLineBufferID = VFD_CreateNewLine(); /* get index for new/next line in DisplayHistory array */
              recallLineNumber = currentLineBufferID; /* make note of current line as the new recall line number */

              sprintf((char *) printBuffer, "\rLine Buffer ID = %d\r\n", currentLineBufferID);
              HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
		  }
		  else if(CTRL_G == rxData) /* clear display */
		  {
			  sprintf((char *) printBuffer, "\r\nClearDisplay = 0x%02x\r\n", rxData);
			  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
              replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
              VFD_ClearDisplay();
		  }
		  else if(DEL == rxData) /* enter pause/single-step mode */
		  {
			  HAL_TIM_Base_Stop_IT(&htim2); /* stop the readback timer */
			  sprintf((char *) printBuffer, "DEL - single stepping line number %d\r\n", recallLineNumber);
			  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
			  singleStepFlag = true;
		  }
		  else if(ESC == rxData) /* ESC key detected ... determine if it's just the ESC key or beginning of an escape sequence */
		  {
              escSequenceNum = 0; /* track the number of characters in the escape sequence (debug) */
              escSequence[escSequenceNum++] = rxData; /* save ESC character for later (also debug) */
              isEchoFlag = false; /* negate flag to prevent escape sequence characters from being echoed */
              isEscapeSequenceFlag = true; /* ESC key detected, escape sequence is (potentially) active */
              HAL_TIM_Base_Start_IT(&htim1); /* start timeout timer (timeout period set to 20ms), will abort sequence if oneshot timer expires */
		  }
          else if(true == isEscapeSequenceFlag) /* escape key was previously detected ... parse escape sequence with state machine */
          {
              isEchoFlag = false; /* negate flag to prevent escape sequence characters from being echoed */
              switch(escSeqState) /* process/parse escape sequence */
              {
                  case ESCAPE: /* ESC key was previously detected, check next character for expected value of 0x5b */
                      if(0x5b == rxData)
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X5B; /* move to next state */
                      }
                      else /* expected character (0x5b) in escape sequence not found ... abort */
                      {
                          isEscapeSequenceFlag = false; /* abort escape sequence processing */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                      }

                      break;

                  case X5B: /* expected character (0x5b) previously found ... keep parsing escape sequence (3rd character in sequence) */
                      if(UP_ARROW == rxData) /* scroll back one line */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                          /* take action here */
                          if(0 == recallLineNumber) /* handle circular boundary */
                              recallLineNumber = NUMBER_PAGES - 1;
                          else
                              recallLineNumber -= 1;
                          sprintf((char *) printBuffer, "UP_ARROW   recall line\t... %3d\t", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                          HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                          HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);
                      }
                      else if(DOWN_ARROW == rxData) /* scroll forward one line */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                          /* take action here */
                          if((NUMBER_PAGES - 1) == recallLineNumber) /* handle circular boundary */
                              recallLineNumber = 0;
                          else
                              recallLineNumber += 1;
                          sprintf((char *) printBuffer, "DOWN_ARROW recall line\t... %3d\t", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                          HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                          HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);
                      }
                      else if(RIGHT_ARROW == rxData) /* replay line, normal playback speed */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                          /* take action here */
                          sprintf((char *) printBuffer, "RIGHT_ARROW (replay line) %d\r\n", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          VFD_ClearDisplay();
                          __HAL_TIM_SetCounter(&htim2, 0);
                          __HAL_TIM_SetAutoreload(&htim2, READBACK_TIMER_PERIOD); /* change readback speed */
                          replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                          HAL_TIM_Base_Start_IT(&htim2); /* readBackFlag (set in readback timer isr) will request the next character */
                      }
                      else if(LEFT_ARROW == rxData) /* replay line, fast playback speed */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                          /* take action here */
                          sprintf((char *) printBuffer, "LEFT_ARROW (replay line) %d\r\n", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          VFD_ClearDisplay();
                          __HAL_TIM_SetCounter(&htim2, 0);
                          __HAL_TIM_SetAutoreload(&htim2, FAST_READBACK_TIMER_PERIOD); /* change readback speed */
                          replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                          HAL_TIM_Base_Start_IT(&htim2); /* readBackFlag (set in readback timer isr) will request the next character */
                      }
                      else if(PAGE_UP == rxData) /* scroll back PAGE_JUMP_SIZE lines */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X7E; /* PAGE_UP is a 4-byte sequence, move to last state */
                          /* take action here */
                          if(recallLineNumber < PAGE_JUMP_SIZE) /* handle circular boundary */
                              recallLineNumber = (NUMBER_PAGES - 1) - (PAGE_JUMP_SIZE - recallLineNumber);
                          else
                              recallLineNumber -= PAGE_JUMP_SIZE;
                          sprintf((char *) printBuffer, "PAGE_UP    recall line\t... %3d\t", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                          HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                          HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);
                      }
                      else if(PAGE_DOWN == rxData) /* scroll forward PAGE_JUMP_SIZE lines */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X7E; /* PAGE_DOWN is a 4-byte sequence, move to last state */
                          /* take action here */
                          if(recallLineNumber >= (NUMBER_PAGES - 1) - PAGE_JUMP_SIZE) /* handle circular boundary */
                              recallLineNumber = PAGE_JUMP_SIZE - ((NUMBER_PAGES - 1) - recallLineNumber);
                          else
                              recallLineNumber += PAGE_JUMP_SIZE;
                          sprintf((char *) printBuffer, "PAGE_DOWN  recall line\t... %3d\t", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                          HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                          HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);
                      }
                      else if(HOME == rxData) /* return to the most recent line */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X7E; /* HOME is a 4-byte sequence, move to last state */
                          /* take action here */
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          recallLineNumber = VFD_ReturnHome(); /* VFD_ReturnHome prints latest line and returns line number */
                          sprintf((char *) printBuffer, "HOME - calling VFD_ReturnHome() ... returned line number %d\r\n", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                      }
                      else if(END == rxData) /* go directly to the oldest line/record */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X7E; /* END is a 4-byte sequence, move to last state */
                          /* take action here */
                          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                          recallLineNumber = VFD_GoToOldest(); /* VFD_GoToOldest searches/finds (then prints) oldest line in history and returns line number */
                          sprintf((char *) printBuffer, "END - calling VFD_GoToOldest() ... returned line number %d\r\n", recallLineNumber);
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                      }
                      else if(INSERT == rxData) /* placeholder for now ... print the deepest FIFO level so far */
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          escSeqState = X7E; /* INSERT is a 4-byte sequence, move to last state */
                          /* take action here */
                          sprintf((char *) printBuffer, "INSERT - fifo_MaxLevelReached = %d out of %d\r\n", fifo_MaxLevelReached, sizeof(rxFIFO));
                          HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          HAL_UART_Transmit(&huart1, (uint8_t*)"restarting readback timer ...\r\n", strlen((char *)"restarting readback timer ...\r\n"), 100);
                          HAL_TIM_Base_Start_IT(&htim2);
                      }
                      else /* unknown/unexpected 3rd character */
                      {
                    	  HAL_UART_Transmit(&huart1, (uint8_t*)"Untracked 3-byte sequence\r\n", strlen((char *)"Untracked 3-byte sequence\r\n"), 100);
                          escSeqState = X7E;
                      }

                      break;

                  case X7E: /* last (4th) character of escape sequence (0x7e) */
                      if(0x7e == rxData)
                      {
                          escSequence[escSequenceNum++] = rxData; /* save character for later use */
                          /* cosmetics/debug ... print captured sequence */
                          HAL_UART_Transmit(&huart1, (uint8_t*)"4-Byte Sequence ... ", strlen((char *)"4-Byte Sequence ... "), 100);
                          for(uint8_t i = 0; i < 4; i++)
                          {
                              sprintf((char *) printBuffer, "%02x ", escSequence[i]);
                              HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                          }
                          HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);
                          isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                          escSeqState = ESCAPE; /* return to initial/idle state */
                      }
                      else /* unexpected 4th character, abort escape sequence parsing */
                      {
                          HAL_UART_Transmit(&huart1, (uint8_t*)"Unexpected 4th character, aborting escape sequence parsing.\r\n", \
                        		  strlen((char *)"Unexpected 4th character, aborting escape sequence parsing.\r\n"), 100);
                          escSeqState = ESCAPE; /* return to initial/idle state */
                      }

                      break;

                  default:

                      break;
              }
          }
          else if(true == isEchoFlag) /* process printable characters here */
          {
              if(true == clearDisplayFlag) /* reminder to clear display if this is the first character of a new line */
              {
            	  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_OFF); /* cosmetics ... LED_OFF indicates new line in progress */
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  VFD_ClearDisplay(); /* this is the first character of a new line, clear display */
                  if(RIGHT_ENTRY == entryMode) /* cosmetic (positions underline at end of display) */
                      VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
                  clearDisplayFlag = false;
              }
              HAL_UART_Transmit(&huart1, &rxData, 1, 100); /* echo received character */
              currentLineBufferID = VFD_PostToHistory(rxData); /* write to display history */
              recallLineNumber = currentLineBufferID; /* drag recallLineNumber along */
              updateDisplayFlag = TRUE; /* indicate need for display update */
          }
	  }

      if(true == timeoutFlag) /* ESC key only, not an escape sequence */
      {
    	  HAL_UART_Transmit(&huart1, (uint8_t*)"ESC\r\n", strlen((char *)"ESC\r\n"), 100); /* ESC key is a way to abandon (escape) a readback by quickly recalling the line */
          isEscapeSequenceFlag = false; /* abort/end escape sequence processing */
          timeoutFlag = false; /* clear the timer timeout interrupt flag */
          /* take action here */
          HAL_TIM_Base_Stop_IT(&htim2); /* don't need the readback timer interrupt any more ... just recall the line */
          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
          VFD_RecallLine(recallLineNumber); /* just paint display quickly */
      }

      if(true == readBackFlag) /* readback in progress, request next character */
      {
          readBackFlag = false; /* clear the readback timer interrupt flag */
          replayCharNumber = VFD_ReplayLine(recallLineNumber, replayCharNumber); /* request a character to be printed to the display */
          if(0 != replayCharNumber) /* check if line is complete */
          {
        	  HAL_TIM_Base_Start_IT(&htim2); /* trigger/start readback oneshot timer ... TC interrupt handler will set readBackFlag */
          }
          else
              replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
      }

      if(true == singleStepFlag) /* singleStep active ... request one character */
      {
          singleStepFlag = false; /* clear the single step flag */
          if(INITIALIZE_REPLAY == replayCharNumber) /* 0xffff indicates that single-step should start at 0 */
          {
              VFD_ClearDisplay();
              replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request a character to be printed to the display */
          }
          else if(0 != replayCharNumber) /* check if line is complete */
          {
              replayCharNumber = VFD_ReplayLine(recallLineNumber, replayCharNumber); /* request another character to be printed to the display */
          }
      }

      if(TRUE == updateDisplayFlag) /* process display updates here */
      {
          cursorPosition = VFD_UpdateDisplay(); /* update/write to display */
          updateDisplayFlag = FALSE;
      }

      if(TRUE == readButtonFlag)
      {
    	  ButtonProcess(&buttons0, rawButtons0);
    	  ButtonProcess(&buttons1, rawButtons1);

    	  readButtonFlag = false;
      }

      buttonsPressed = (ButtonCurrent(&buttons1, BUTTON_PIN_0 | BUTTON_PIN_1 | BUTTON_PIN_2 | BUTTON_PIN_3 | BUTTON_PIN_4 | BUTTON_PIN_5 | BUTTON_PIN_6 | BUTTON_PIN_7)) << 8;
      buttonsPressed |= ButtonCurrent(&buttons0, BUTTON_PIN_0 | BUTTON_PIN_1 | BUTTON_PIN_2 | BUTTON_PIN_3 | BUTTON_PIN_4 | BUTTON_PIN_5 | BUTTON_PIN_6 | BUTTON_PIN_7);
      if(buttonsPressed != previousButtonsPressed)
	  {
//		  sprintf((char *) printBuffer, "Buttons pressed = 0x%04x\r\n", buttonsPressed);
//		  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
		  previousButtonsPressed = buttonsPressed;

		  switch(buttonsPressed)
		  {
		  	  case	NONE:

		  		break;

		  	  case	SCROLL_UP:
		  		  if(0 == recallLineNumber) /* handle circular boundary */
		  			  recallLineNumber = NUMBER_PAGES - 1;
		  		  else
		  			  recallLineNumber -= 1;
		  		  sprintf((char *) printBuffer, "Scroll up,   recall line ... %3d  ", recallLineNumber);
		  		  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
		  		  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
		  		  str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
		  		  HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
		  		  HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);

		  		break;

		  	  case	SCROLL_DOWN:
                  if((NUMBER_PAGES - 1) == recallLineNumber) /* handle circular boundary */
                      recallLineNumber = 0;
                  else
                      recallLineNumber += 1;
                  sprintf((char *) printBuffer, "Scroll down, recall line ... %3d  ", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                  HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                  HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);

		  		break;

		  	  case	SINGLE_STEP: /* press&release reads back one character at a time, press&hold readbacks characters at half normal readback speed */
				  HAL_TIM_Base_Stop_IT(&htim2); /* stop the readback timer */
				  timerRestorValue = __HAL_TIM_GetAutoreload(&htim2); /* save timer value for later restore */
                  __HAL_TIM_SetCounter(&htim2, 0); /* reset counter */
                  __HAL_TIM_SetAutoreload(&htim2, SINGLE_STEP_TIMER_PERIOD); /* change readback speed */
                  HAL_TIM_Base_Start_IT(&htim2); /* restart the timer ... interrupts will readback another character */
				  sprintf((char *) printBuffer, "Single stepping line number %d\r\n", recallLineNumber);
				  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
				  singleStepFlag = true;

		  		break;

		  	  case	RESUME: /* escape single-step mode (i.e. resume readback */
                  sprintf((char *) printBuffer, "fifo_MaxLevelReached = %d out of %d\r\n", fifo_MaxLevelReached, sizeof(rxFIFO));
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  HAL_UART_Transmit(&huart1, (uint8_t*)"restarting readback timer ...\r\n", strlen((char *)"restarting readback timer ...\r\n"), 100);
                  HAL_TIM_Base_Start_IT(&htim2);

		  		break;

		  	  case	READBKF: /* Readback (replay) current line at fast speed */
                  sprintf((char *) printBuffer, "Replay line (fast) %d\r\n", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  VFD_ClearDisplay();
                  __HAL_TIM_SetCounter(&htim2, 0); /* reset counter */
                  __HAL_TIM_SetAutoreload(&htim2, FAST_READBACK_TIMER_PERIOD); /* change readback speed */
                  replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                  HAL_TIM_Base_Start_IT(&htim2); /* readBackFlag (set in readback timer isr) will request the next character */

		  		break;

		  	  case	READBK: /* Readback (replay) current line at normal speed */
                  sprintf((char *) printBuffer, "Replay line (normal speed) %d\r\n", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  VFD_ClearDisplay();
                  __HAL_TIM_SetCounter(&htim2, 0);
                  __HAL_TIM_SetAutoreload(&htim2, READBACK_TIMER_PERIOD); /* change readback speed */
                  replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                  HAL_TIM_Base_Start_IT(&htim2); /* readBackFlag (set in readback timer isr) will request the next character */

		  		break;

		  	  case	ESCRB: /* (ESCapeReadBack) - return immediately to end of current line */
                  sprintf((char *) printBuffer, "Return to end of line %d\r\n", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
		  		  HAL_TIM_Base_Stop_IT(&htim2); /* don't need the readback timer interrupt any more ... just recall the line */
		          replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
		          VFD_RecallLine(recallLineNumber); /* just paint display quickly */

		  		break;

		  	  case	PAGEUP: /* scroll back PAGE_JUMP_SIZE lines */
                  if(recallLineNumber < PAGE_JUMP_SIZE) /* handle circular boundary */
                      recallLineNumber = (NUMBER_PAGES - 1) - (PAGE_JUMP_SIZE - recallLineNumber);
                  else
                      recallLineNumber -= PAGE_JUMP_SIZE;
                  sprintf((char *) printBuffer, "Recall line\t... %3d\t", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                  HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                  HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);

		  		break;

		  	  case	PAGEDN: /* scroll forward PAGE_JUMP_SIZE lines */
                  if(recallLineNumber >= (NUMBER_PAGES - 1) - PAGE_JUMP_SIZE) /* handle circular boundary */
                      recallLineNumber = PAGE_JUMP_SIZE - ((NUMBER_PAGES - 1) - recallLineNumber);
                  else
                      recallLineNumber += PAGE_JUMP_SIZE;
                  sprintf((char *) printBuffer, "Recall line\t... %3d\t", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  str = VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                  HAL_UART_Transmit(&huart1, str, strlen((char *) str), 100);
                  HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n", 2, 100);

		  		break;

		  	  case	GOHOME: /* GoHome ... return to the most recent line */
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  recallLineNumber = VFD_ReturnHome(); /* VFD_ReturnHome prints latest line and returns line number */
                  sprintf((char *) printBuffer, "Return home - calling VFD_ReturnHome() ... returned line number %d\r\n", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

		  		break;

		  	  case	GOEND: /* GoToEnd ... go directly to the oldest line/record */
                  replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                  recallLineNumber = VFD_GoToOldest(); /* VFD_GoToOldest searches/finds (then prints) oldest line in history and returns line number */
                  sprintf((char *) printBuffer, "END - calling VFD_GoToOldest() ... returned line number %d\r\n", recallLineNumber);
                  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

		  		break;


		  	  default:
		  		HAL_UART_Transmit(&huart1, (uint8_t*)"Unassigned key\r\n", strlen((char *)"Unassigned key\r\n"), 100);

		  		break;

		  }
      }

      if(ButtonReleased(&buttons1, (uint8_t) (SINGLE_STEP >> 8)))
      {
		  HAL_TIM_Base_Stop_IT(&htim2); /* stop the readback timer */
		  __HAL_TIM_SetAutoreload(&htim2, timerRestorValue); /* restore timer value to what it was before single stepping */
          __HAL_TIM_SetCounter(&htim2, 0); /* reset counter */
      }


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 7200-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 200-1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7200-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 200;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 40000-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 18-1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(nTEST_GPIO_Port, nTEST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, D0_Pin|D1_Pin|D2_Pin|D3_Pin
                          |D4_Pin|D5_Pin|D6_Pin|D7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, nCS_Pin|nWR_Pin|nRD_Pin|Col3_Pin
                          |Col2_Pin|Col1_Pin|Col0_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED1_Pin nTEST_Pin */
  GPIO_InitStruct.Pin = LED1_Pin|nTEST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : D0_Pin D1_Pin D2_Pin D3_Pin
                           D4_Pin D5_Pin D6_Pin D7_Pin */
  GPIO_InitStruct.Pin = D0_Pin|D1_Pin|D2_Pin|D3_Pin
                          |D4_Pin|D5_Pin|D6_Pin|D7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : nCS_Pin A0_Pin nWR_Pin nRD_Pin */
  GPIO_InitStruct.Pin = nCS_Pin|A0_Pin|nWR_Pin|nRD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : Col3_Pin Col2_Pin Col1_Pin Col0_Pin */
  GPIO_InitStruct.Pin = Col3_Pin|Col2_Pin|Col1_Pin|Col0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : Row3_Pin */
  GPIO_InitStruct.Pin = Row3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(Row3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Row2_Pin Row1_Pin Row0_Pin */
  GPIO_InitStruct.Pin = Row2_Pin|Row1_Pin|Row0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void delay_us(uint16_t delay)
{
	if(delay < 1)
		delay = 1;
	else delay = (delay - 1) * 4;
	__HAL_TIM_SetCounter(&htim4, 0);
	while(__HAL_TIM_GetCounter(&htim4) < delay);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	rxFIFO[headPointer++] = rxBuffer[0]; /* place received character from UART in FIFO */
	if(headPointer >= UART_FIFO_SIZE) /* manage headPointer rollover (new arrivals will overwrite older) */
		headPointer = 0;

	/* calculate MAX FIFO level reached ... adjust formula based on rollover */
	if(headPointer >= tailPointer) /* normal/easy calculation if headPointer is ahead of tailPointer */
	{
		if(headPointer - tailPointer > fifo_MaxLevelReached)
			fifo_MaxLevelReached = headPointer - tailPointer;
	}
	else /* adjusted calculation if tailPointer is ahead of headPointer */
	{
		if(headPointer + UART_FIFO_SIZE - tailPointer > fifo_MaxLevelReached)
			fifo_MaxLevelReached = headPointer + UART_FIFO_SIZE - tailPointer;
	}
    HAL_UART_Receive_IT(&huart1, rxBuffer, 1); /* restart UART Rx interrupt */
}

/**
  * TIM1 - escape sequence time-out timer, TIM2 readback timer
  *
  * @brief  Period elapsed callback in non blocking mode
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	static uint8_t columnCounter;
	if(htim->Instance == TIM1)
	{
		timeoutFlag = true; /* set timeOut flag to indicate escape sequence parsing should be abandoned/aborted */
		HAL_TIM_Base_Stop_IT(htim);
	}
	if(htim->Instance == TIM2)
	{
		readBackFlag = true; /* set flag to indicate that a new readback character can be requested */
		HAL_TIM_Base_Stop_IT(htim);
	}
	if(htim->Instance == TIM3) /* switch matrix multiplexing */
	{
  		switch(columnCounter++)
  		{
  			case 0:
  				rawButtons0 &= 0xf0;
  				HAL_GPIO_WritePin(Col1_GPIO_Port, Col1_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col2_GPIO_Port, Col2_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col3_GPIO_Port, Col3_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col0_GPIO_Port, Col0_Pin, COLUMN_ACTIVE);
  				delay_us(INTRA_COLUMN_DELAY_US);
  				rawButtons0 = (!HAL_GPIO_ReadPin(Row0_GPIO_Port, Row0_Pin)) | (!HAL_GPIO_ReadPin(Row1_GPIO_Port, Row1_Pin)) << 1 | \
  						 (!HAL_GPIO_ReadPin(Row2_GPIO_Port, Row2_Pin)) << 2 | (!HAL_GPIO_ReadPin(Row3_GPIO_Port, Row3_Pin)) << 3;

  				break;

  			case 1:
  				rawButtons0 &= 0x0f;
  				HAL_GPIO_WritePin(Col0_GPIO_Port, Col0_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col2_GPIO_Port, Col2_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col3_GPIO_Port, Col3_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col1_GPIO_Port, Col1_Pin, COLUMN_ACTIVE);
  				delay_us(INTRA_COLUMN_DELAY_US);
  				rawButtons0 |= ((!HAL_GPIO_ReadPin(Row0_GPIO_Port, Row0_Pin)) | (!HAL_GPIO_ReadPin(Row1_GPIO_Port, Row1_Pin)) << 1 | \
  						 (!HAL_GPIO_ReadPin(Row2_GPIO_Port, Row2_Pin)) << 2 | (!HAL_GPIO_ReadPin(Row3_GPIO_Port, Row3_Pin)) << 3) << 4;

  				break;

  			case 2:
  				rawButtons1 &= 0xf0;
  				HAL_GPIO_WritePin(Col0_GPIO_Port, Col0_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col1_GPIO_Port, Col1_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col3_GPIO_Port, Col3_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col2_GPIO_Port, Col2_Pin, COLUMN_ACTIVE);
  				delay_us(INTRA_COLUMN_DELAY_US);
  				rawButtons1 = (!HAL_GPIO_ReadPin(Row0_GPIO_Port, Row0_Pin)) | (!HAL_GPIO_ReadPin(Row1_GPIO_Port, Row1_Pin)) << 1 | \
  						 (!HAL_GPIO_ReadPin(Row2_GPIO_Port, Row2_Pin)) << 2 | (!HAL_GPIO_ReadPin(Row3_GPIO_Port, Row3_Pin)) << 3;

  				break;

  			case 3:
  				rawButtons1 &= 0x0f;
  				HAL_GPIO_WritePin(Col0_GPIO_Port, Col0_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col1_GPIO_Port, Col1_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col2_GPIO_Port, Col2_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col3_GPIO_Port, Col3_Pin, COLUMN_ACTIVE);
  				delay_us(INTRA_COLUMN_DELAY_US);
  				rawButtons1 |= ((!HAL_GPIO_ReadPin(Row0_GPIO_Port, Row0_Pin)) | (!HAL_GPIO_ReadPin(Row1_GPIO_Port, Row1_Pin)) << 1 | \
  						 (!HAL_GPIO_ReadPin(Row2_GPIO_Port, Row2_Pin)) << 2 | (!HAL_GPIO_ReadPin(Row3_GPIO_Port, Row3_Pin)) << 3) << 4;

  				break;

  			default:
  				HAL_GPIO_WritePin(Col0_GPIO_Port, Col0_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col1_GPIO_Port, Col1_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col2_GPIO_Port, Col2_Pin, COLUMN_INACTIVE);
  				HAL_GPIO_WritePin(Col3_GPIO_Port, Col3_Pin, COLUMN_INACTIVE);

  				break;
  		}

  		if(columnCounter >= NUM_DISPLAY_COLUMNS)
  		{
  			columnCounter = 0;
  			readButtonFlag = true;
  		}
	}
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
