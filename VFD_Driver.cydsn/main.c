/* ========================================
 *
 * File Name:   main.c
 * 
 * Date:    5-Sept-2023
 * Author:  David Waskevich
 *
 * Description: Test application for IEE FLIP 03600-20-040 Vacuum
 *              Fluorescent Display.
 * 
 * Hardware:    Sparkfun FreeSOC2 (https://www.sparkfun.com/products/retired/13714)
 *              PSoC5LP/CortexM3-based (enhanced) Arduino-style development kit
 *
 * IDE:         PSoC Creator 4.3
 *
 * Wiring:      8-bit parallel data bus to display --> P2[7:0] (JP5 header)
 *              /CS - P6[4]  (JP7, pin 3)
 *               A0 - P12[5] (JP7, pin 2)
 *              /WR - P12[4] (JP7, pin 1)
 *              /RD - not used
 * 
 * Usage:       #include <iee_flip_03600_20_040.h>
 *              NOTE - arbitrarily chose BELL (ctrl-G) character to reset display.
 *
 * Update 7-Sept-2023:
 *		- added iee_flip_03600_20_040.h and iee_flip_03600_20_040.c source files
 *		- updated test script in main.c
 *
 * Update 16-Sept-2023:
 *		- added low-level hardware-dependent drivers for parallel port and ctrl
 *        lines (to simplify hardware independence/migration)
 *
 * Update 5-Jan-2024:
 *		- created "develop" branch
 *      - enhancing driver to handle left and right (crawl/scroll) entry
 *
 * Update 10-Jan-2024:
 *		- moved scrolling code from main.c to iee_flip_03600_20_040.c/.h
 *      - created new functions
 *      - updated main.c
 *      - entry mode (LEFT/RIGHT) defined in .h file
 *          '-> returned to application with VFD_InitializeDisplay() API
 *      - set up desired INPUT_BUFFER_LENGTH and DISPLAY_LINE_LENGTH in .h file
 *      - length of history buffer depends on available SRAM
 *          '-> set NUMBER_PAGES in .h file 
 *
 * Update 4-Aug-2024:
 *		- improvements to 10-Jan-2024 update
 *      - added state machine to detect ESC key and escae sequences
 *          -> One-shot timer generates an interrupt longer than 115,200
 *             arrival time (empirical value = 20msec)
 *
 * Update 6-Aug-2024:
 *		- moved UART Rx processing to ISR
 *          -> UART_FIFO_SIZE set with #define
 *      - implemented routines for UP_ARROW, DOWN_ARROW and HOME
 *
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include "project.h"
#include "iee_flip_03600_20_040.h"
#include "stdio.h"
#include "stdbool.h"

#define LED_OFF     (0u)
#define LED_ON      (1u)

#define UART_FIFO_SIZE    (2048u)

CY_ISR_PROTO(timerISR);
CY_ISR_PROTO(uartISR);

volatile bool timeoutFlag = false;
volatile uint16_t headPointer = 0, tailPointer = 0;
char rxFIFO[UART_FIFO_SIZE];

volatile uint16_t fifoLevel;

/* Escape sequence state machine */
enum escSeqStates
{
    ESCAPE, /* ESC character (0x1b) detected */
    X5B,    /* looking for second escape sequence character (0x5b) */
    X7E     /* some keys (HOME, END, INSERT) generate 4-byte sequence with last character = 0x7e */
};
enum escSeqStates escSeqState = ESCAPE;

int main(void)
{
    char printBuffer[100]; /* used for sprintf debugging */
    char rxData;
    uint8_t entryMode, cursorPosition;
    uint16_t currentLineBufferID = 0;
    uint8_t updateDisplayFlag = FALSE;
    bool isEchoFlag = true;
    bool isEscapeSequenceFlag = false;
    char escSequence[4] = {0};
    uint8_t escSequenceNum = 0;
    bool clearDisplayFlag = false;
    static uint16_t recallLineNumber = 0;
        
    CyGlobalIntEnable; /* Enable global interrupts. */
    
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    /* initialize one-shot timer (distinguishes ESC key from escape sequences) */
    Timer_SetInterruptMode(Timer_STATUS_TC_INT_MASK );
    isr_timeout_StartEx(timerISR);
    
    /* start UART interrupt handler */
    isr_UART_StartEx(uartISR);
    
    /* initialize VFD display (returns entry mode defined in .h file) */
    entryMode = VFD_InitializeDisplay(DEFAULT_ENTRY_MODE);
    
    /* initialize and start UART */
    UART_Start();    
    UART_PutString("\r\nUART started ...\r\n");
    
    /* initialize display history */
    sprintf(printBuffer, "Initializing display history. Number of pages = %d\r\n", VFD_InitDisplayHistory());
    UART_PutString(printBuffer);
    
    sprintf(printBuffer, "SRAM usage for display history = %d\r\n", VFD_SizeOfHistoryArray());
    UART_PutString(printBuffer);
    
    while(1)
    {
        /* check rxFIFO for incoming characters */
        if(tailPointer != headPointer) /* if true, new data is available */
        {
            rxData = rxFIFO[tailPointer++]; /* retrieve new character from FIFO */
            if(tailPointer >= UART_FIFO_SIZE) /* manage FIFO pointer rollover */
                tailPointer = 0;
            isEchoFlag = true; /* set flag on each new character received (true if printable character, will be reset to false if escape sequence is detected) */
            Timer_Stop(); /* stop the ESC timeout timer on each new character received */
            
            /* parse incoming characters for carriage return and/or line feed */
            if(CR == rxData || LF == rxData) /* handle CR/LF here */
            {
                if(CR == rxData)
                    UART_PutChar(LF);
                    
                if(LF == rxData)
                    UART_PutChar(CR);
                    
                clearDisplayFlag = true; /* reminder to clear display on next received character */
                UserLED_Write(LED_ON); /* UserLED "ON" to indicate end-of-line (display clear pending) */
                
//                if(RIGHT_ENTRY == entryMode) /* cosmetic (positions underline at end of display) */
//                    VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
                
                currentLineBufferID = VFD_CreateNewLine(); /* get index for next/new line in DisplayHistory array */
                recallLineNumber = currentLineBufferID; /* make note of current line as the new recall line number */
                
                sprintf(printBuffer, "\rLine Buffer ID = %d\r\n", currentLineBufferID);
                UART_PutString(printBuffer);
            }
            else if(ESC == rxData) /* ESC key detected ... determine if it's just the ESC key or beginning of an escape sequence */
            {
                escSequenceNum = 0; /* track the number of characters in the escape sequence */
                escSequence[escSequenceNum++] = rxData; /* save ESC character for later */
                isEchoFlag = false; /* negate flag to prevent escape sequence characters from being echoed */
                isEscapeSequenceFlag = true; /* ESC key detected, escape sequence is (potentially) active */
                Timer_Start(); /* start timeout timer (timeout period set to 20ms), will abort sequence if oneshot timer expires */
            }
            else if(true == isEscapeSequenceFlag)
            {
                isEchoFlag = false; /* negate flag to prevent escape sequence characters from being echoed */
                switch(escSeqState) /* process/parse escape sequence */
                {
                    case ESCAPE: /* ESC key was detected, check next character for expected value of 0x5b */
                        if(0x5b == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            escSeqState = X5B; /* move to next state */
                        }
                        else /* expected character (0x5b) in escape sequence not found ... abandon */
                        {
                            isEscapeSequenceFlag = false; /* abandon escape sequence processing */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                        }
                    
                        break;
                    
                    case X5B: /* expected character (0x5b) found ... keep parsing escape sequence (3rd character in sequence) */
                        if(UP_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                            /* take appropriate action */
                            if(0 == recallLineNumber)
                                recallLineNumber = NUMBER_PAGES - 1;
                            else
                                recallLineNumber -= 1;
                            sprintf(printBuffer, "UP_ARROW (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            VFD_RecallLine(recallLineNumber);
                        }
                        else if(DOWN_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                            /* take appropriate action */
                            if((NUMBER_PAGES - 1) == recallLineNumber)
                                recallLineNumber = 0;
                            else
                                recallLineNumber += 1;
                            sprintf(printBuffer, "DOWN_ARROW (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            VFD_RecallLine(recallLineNumber);
                        }
                        else if(RIGHT_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            UART_PutString("RIGHT_ARROW\r\n");
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                        }
                        else if(LEFT_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            UART_PutString("LEFT_ARROW (replay line)\r\n");
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                            VFD_ReplayLine(--currentLineBufferID);
                        }
                        else if(PAGE_UP == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            UART_PutString("PAGE_UP\r\n");
                            escSeqState = X7E; /* PAGE_UP is a 4-byte sequence, move to last state */
                        }
                        else if(PAGE_DOWN == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            UART_PutString("PAGE_DOWN\r\n");
                            escSeqState = X7E; /* PAGE_DOWN is a 4-byte sequence, move to last state */
                        }
                        else if(HOME == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            escSeqState = X7E; /* HOME is a 4-byte sequence, move to last state */
                            /* take appropriate action */
                            recallLineNumber = currentLineBufferID;
                            sprintf(printBuffer, "HOME - recall line number = %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            VFD_RecallLine(recallLineNumber);
                        }
                        else if(END == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            UART_PutString("END\r\n");
                            escSeqState = X7E; /* END is a 4-byte sequence, move to last state */
                        }
                        else if(INSERT == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
//                            UART_PutString("INSERT\r\n");
                            escSeqState = X7E; /* INSERT is a 4-byte sequence, move to last state */
                            /* take appropriate action */
                            sprintf(printBuffer, "INSERT - fifoLevel = %d\r\n", fifoLevel);
                            UART_PutString(printBuffer);
                        }
                        else /* unknown/unexpected 3rd character */
                        {
                            UART_PutString("Untracked 3-byte sequence\r\n");
                            escSeqState = X7E;
                        }
                    
                        break;
                    
                    case X7E: /* last (4th) character of escape sequence (0x7e) */
                        if(0x7e == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            /* cosmetics/debug ... print captured sequence */
                            UART_PutString("4-Byte Sequence ... ");
                            for(uint8_t i = 0; i < 4; i++)
                            {
                                sprintf(printBuffer, "%02x ", escSequence[i]);
                                UART_PutString(printBuffer);
                            }
                            UART_PutString("\r\n");
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                        }
                        else /* unexpected 4th character, abandon escape sequence parsing */
                        {
                            UART_PutString("Unexpected 4th character, abandoning escape sequence parsing.\r\n");
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
                    UserLED_Write(LED_OFF); /* cosmetics ... LED_OFF indicates new line in progress */
                    VFD_ClearDisplay(); /* this is the first character of a new line, clear display */
                    if(RIGHT_ENTRY == entryMode) /* cosmetic (positions underline at end of display) */
                        VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
                    clearDisplayFlag = false;
                }
                UART_PutChar(rxData); /* echo received character */
                currentLineBufferID = VFD_PostToHistory(rxData); /* write to display history */
                recallLineNumber = currentLineBufferID - 1; /* drag recallLineNumber along */
                updateDisplayFlag = TRUE; /* signal need for display update */
            }
            
            if(CTRL_G == rxData)
            {
                sprintf(printBuffer, "\r\nClearDisplay = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                VFD_ClearDisplay();
            }
        }
        
        if(true == timeoutFlag) /* ESC key only, not an escape sequence */
        {
            UART_PutString("ESC\r\n"); /* placeholder for something useful later ... like ESC function */
            isEscapeSequenceFlag = false; /* abort/end escape sequence processing */
            timeoutFlag = false; /* clear the timer timeout interrupt flag */
        }
        
        if(TRUE == updateDisplayFlag) /* process display updates here */
        {
            cursorPosition = VFD_UpdateDisplay(); /* update/write to display */
            updateDisplayFlag = FALSE;
        }
        
        if(0 == User_BTN_Read())
        {
            CyDelay(150);
            VFD_Test(User_BTN_Read());
            while(0 == User_BTN_Read())
            ;
            UART_PutCRLF('x');
            VFD_Test(1);
        }
    }
}

CY_ISR(timerISR)
{
    timeoutFlag = true; /* set timeOut flag */
    Timer_STATUS; /* read timer Status to clear "sticky" interrupt bit */
    Timer_Stop(); /* stopping the timer reloads the period counter with configuration value */
    isr_timeout_ClearPending(); /* clear the pending interrupt in the isr component */
} 

CY_ISR(uartISR)
{
    rxFIFO[headPointer++] = UART_GetChar(); /* place received character from UART in FIFO */
    if(headPointer >= UART_FIFO_SIZE) /* manage headPointer rollover */
        headPointer = 0;
    if(headPointer - tailPointer > fifoLevel)
        fifoLevel = headPointer - tailPointer;
//    UART_ReadRxStatus(); /* not needed ... UART_RX_STS_FIFO_NOTEMPTY clears immediately after RX data register read. */
    isr_UART_ClearPending(); /* clear the pending interrupt in the isr component */
} 

/* [] END OF FILE */
