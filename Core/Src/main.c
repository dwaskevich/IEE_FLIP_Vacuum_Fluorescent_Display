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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
volatile uint16_t fifo_MaxLevelReached;
uint8_t rxBuffer[80];
volatile uint16_t headPointer = 0, tailPointer = 0;
uint8_t rxFIFO[UART_FIFO_SIZE];

volatile bool timeoutFlag = false, readBackFlag = false, singleStepFlag = false;

/* Escape sequence state machine */
enum escSeqStates
{
    ESCAPE, /* ESC character (0x1b) detected */
    X5B,    /* looking for second escape sequence character (0x5b) */
    X7E     /* some keys (HOME, END, INSERT) generate 4-byte sequence with last character = 0x7e */
};
enum escSeqStates escSeqState = ESCAPE;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

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
  /* USER CODE BEGIN 2 */

  HAL_UART_Receive_IT(&huart1, rxBuffer, 1);
  HAL_UART_Transmit(&huart1, (uint8_t *) "\x1b[2J\x1b[;HUART started\r\n", sizeof("\x1b[2J\x1b[;HUART started\r\n"), 500);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_OFF);

  /* initialize display history */
  sprintf((char *) printBuffer, "Initializing display history. Number of pages = %d\r\n", VFD_InitDisplayHistory());
  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

  sprintf((char *) printBuffer, "SRAM usage for display history = %d\r\n", VFD_GetSizeOfHistoryArray());
  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);

  /* clear timer interrupts before starting forever loop */
  __HAL_TIM_CLEAR_IT(&htim1, TIM_FLAG_UPDATE);
  __HAL_TIM_CLEAR_IT(&htim2, TIM_FLAG_UPDATE);

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
//              HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_ON); /* UserLED "ON" to indicate end-of-line (display clear pending) */

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
			  HAL_TIM_Base_Stop_IT(&htim2);
			  sprintf((char *) printBuffer, "DEL - single stepping line number %d\r\n", recallLineNumber);
			  HAL_UART_Transmit(&huart1, printBuffer, strlen((char *) printBuffer), 100);
			  singleStepFlag = true;
		  }
		  else if(ESC == rxData) /* ESC key detected ... determine if it's just the ESC key or beginning of an escape sequence */
		  {
              escSequenceNum = 0; /* track the number of characters in the escape sequence */
              escSequence[escSequenceNum++] = rxData; /* save ESC character for later */
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
//            	  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, LED_OFF); /* cosmetics ... LED_OFF indicates new line in progress */
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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED1_Pin */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED1_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	rxFIFO[headPointer++] = rxBuffer[0]; /* place received character from UART in FIFO */
	if(headPointer >= UART_FIFO_SIZE) /* manage headPointer rollover */
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
    HAL_UART_Receive_IT(&huart1, rxBuffer, 1);
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
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
