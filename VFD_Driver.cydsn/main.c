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
 * Update 8-Aug-2024:
 *		- added END functionality (finds and recalls oldest line in history)
 *      - added PAGE_UP and PAGE_DOWN features (scrolls forward/backward PAGE_JUMP_SIZE lines)
 *
 * Update 10-Aug-2024:
 *		- added readback timer and isr
 *      - modified VFD_ReplayLine() to be interrupt-driven (removed CyDelay calls)
 *      - implemented ESC function (escapes from replay_line ... fast-forwards to EOL)
 *      - implemented single-step function (DEL key) ... replays one character at a time
 *      - fixed bug in VFD_GoToOldest() ... break from the while loop if history is fresh/new
 *      - moved "clear display" (ctrl-G) to if-else section
 *
 * TODO: remove all the escape sequence debugging code
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

#define INITIALIZE_REPLAY   (0xffff)

#define UART_FIFO_SIZE_PERCENT  (25u)
#define UART_FIFO_SIZE          ((CYDEV_SRAM_SIZE / 100) * UART_FIFO_SIZE_PERCENT)

#define PAGE_JUMP_SIZE          (10u)

CY_ISR_PROTO(timerISR);
CY_ISR_PROTO(uartISR);
CY_ISR_PROTO(readbackISR);

volatile bool timeoutFlag = false, readBackFlag = false, singleStepFlag = false;
volatile uint16_t headPointer = 0, tailPointer = 0;
char rxFIFO[UART_FIFO_SIZE];

volatile uint16_t fifo_MaxLevelReached;

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
    static uint16_t replayCharNumber = 0;
        
    CyGlobalIntEnable; /* Enable global interrupts. */
    
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    /* initialize timeout timer (distinguishes ESC key from escape sequences) */
    Timer_Timeout_SetInterruptMode(Timer_Timeout_STATUS_TC_INT_MASK );
    isr_timeout_StartEx(timerISR); /* register interrupt handler */
    
    /* initialize readback timer (paces readback speed) */
    Timer_Readback_SetInterruptMode(Timer_Readback_STATUS_TC_INT_MASK );
    isr_readback_StartEx(readbackISR); /* register interrupt handler */
    
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
    
    sprintf(printBuffer, "SRAM usage for display history = %d\r\n", VFD_GetSizeOfHistoryArray());
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
            Timer_Timeout_Stop(); /* stop the ESC timeout timer on each new character received */
            
            /* parse incoming characters for carriage return and/or line feed */
            if(CR == rxData || LF == rxData) /* handle CR/LF here */
            {
                if(CR == rxData)
                    UART_PutChar(LF); /* echo back */
                    
                if(LF == rxData)
                    UART_PutChar(CR); /* echo back */
                    
                replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                clearDisplayFlag = true; /* reminder to clear display on next received character (style/aesthetic choice) */
                UserLED_Write(LED_ON); /* UserLED "ON" to indicate end-of-line (display clear pending) */
                
                currentLineBufferID = VFD_CreateNewLine(); /* get index for new/next line in DisplayHistory array */
                recallLineNumber = currentLineBufferID; /* make note of current line as the new recall line number */
                
                sprintf(printBuffer, "\rLine Buffer ID = %d\r\n", currentLineBufferID);
                UART_PutString(printBuffer);
            }
            else if(CTRL_G == rxData)
            {
                sprintf(printBuffer, "\r\nClearDisplay = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                VFD_ClearDisplay();
            }
            else if(DEL == rxData)
            {
                Timer_Readback_Stop();
                sprintf(printBuffer, "DEL - single stepping line number %d\r\n", recallLineNumber);
                UART_PutString(printBuffer);
                singleStepFlag = true;
            }
            else if(ESC == rxData) /* ESC key detected ... determine if it's just the ESC key or beginning of an escape sequence */
            {
                escSequenceNum = 0; /* track the number of characters in the escape sequence */
                escSequence[escSequenceNum++] = rxData; /* save ESC character for later */
                isEchoFlag = false; /* negate flag to prevent escape sequence characters from being echoed */
                isEscapeSequenceFlag = true; /* ESC key detected, escape sequence is (potentially) active */
                Timer_Timeout_Start(); /* start timeout timer (timeout period set to 20ms), will abort sequence if oneshot timer expires */
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
                            sprintf(printBuffer, "UP_ARROW (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
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
                            sprintf(printBuffer, "DOWN_ARROW (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                        }
                        else if(RIGHT_ARROW == rxData) /* replay line, normal playback speed */
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                            /* take action here */
                            sprintf(printBuffer, "RIGHT_ARROW (replay line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            VFD_ClearDisplay();
                            Timer_Readback_WritePeriod(READBACK_TIMER_PERIOD); /* change readback speed */
                            replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                            Timer_Readback_Start(); /* readBackFlag (set in readback timer isr) will request the next character */
                        }
                        else if(LEFT_ARROW == rxData) /* replay line, fast playback speed */
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            isEscapeSequenceFlag = false; /* escape sequence complete, return to normal mode */
                            escSeqState = ESCAPE; /* return to initial/idle state */
                            /* take action here */
                            sprintf(printBuffer, "LEFT_ARROW (replay line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            VFD_ClearDisplay();
                            Timer_Readback_WritePeriod(FAST_READBACK_TIMER_PERIOD); /* change readback speed */
                            replayCharNumber = VFD_ReplayLine(recallLineNumber, 0); /* request to write character to display */
                            Timer_Readback_Start(); /* readBackFlag (set in readback timer isr) will request the next character */
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
                            sprintf(printBuffer, "PAGE_UP (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
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
                            sprintf(printBuffer, "PAGE_DOWN (recall line) %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            VFD_RecallLine(recallLineNumber); /* recall line from history and write it to display */
                        }
                        else if(HOME == rxData) /* return to the most recent line */
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            escSeqState = X7E; /* HOME is a 4-byte sequence, move to last state */
                            /* take action here */
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            recallLineNumber = VFD_ReturnHome(); /* VFD_ReturnHome prints latest line and returns line number */
                            sprintf(printBuffer, "HOME - calling VFD_ReturnHome() ... returned line number %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);
                        }
                        else if(END == rxData) /* go directly to the oldest line/record */
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            escSeqState = X7E; /* END is a 4-byte sequence, move to last state */
                            /* take action here */
                            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                            recallLineNumber = VFD_GoToOldest(); /* VFD_GoToOldest searches/finds (then prints) oldest line in history and returns line number */
                            sprintf(printBuffer, "END - calling VFD_GoToOldest() ... returned line number %d\r\n", recallLineNumber);
                            UART_PutString(printBuffer);                            
                        }
                        else if(INSERT == rxData) /* placeholder for now ... print the deepest FIFO level so far */
                        {
                            escSequence[escSequenceNum++] = rxData; /* save character for later use */
                            escSeqState = X7E; /* INSERT is a 4-byte sequence, move to last state */
                            /* take action here */
                            sprintf(printBuffer, "INSERT - fifo_MaxLevelReached = %d out of %d\r\n", fifo_MaxLevelReached, sizeof(rxFIFO));
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
                        else /* unexpected 4th character, abort escape sequence parsing */
                        {
                            UART_PutString("Unexpected 4th character, aborting escape sequence parsing.\r\n");
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
                    replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
                    VFD_ClearDisplay(); /* this is the first character of a new line, clear display */
                    if(RIGHT_ENTRY == entryMode) /* cosmetic (positions underline at end of display) */
                        VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
                    clearDisplayFlag = false;
                }
                UART_PutChar(rxData); /* echo received character */
                currentLineBufferID = VFD_PostToHistory(rxData); /* write to display history */
                recallLineNumber = currentLineBufferID; /* drag recallLineNumber along */
                updateDisplayFlag = TRUE; /* indicate need for display update */
            }
        }
        
        if(true == timeoutFlag) /* ESC key only, not an escape sequence */
        {
            UART_PutString("ESC\r\n"); /* ESC key is a way to abandon (escape) a readback by quickly recalling the line */
            isEscapeSequenceFlag = false; /* abort/end escape sequence processing */
            timeoutFlag = false; /* clear the timer timeout interrupt flag */
            /* take action here */
            Timer_Readback_Stop(); /* don't need the readback timer interrupt any more ... just recall the line */
            replayCharNumber = INITIALIZE_REPLAY; /* indicates that single-step should start at 0 */
            VFD_RecallLine(recallLineNumber); /* just paint display quickly */
        }
        
        if(true == readBackFlag) /* readback in progress, request next character */
        {
            readBackFlag = false; /* clear the readback timer interrupt flag */
            replayCharNumber = VFD_ReplayLine(recallLineNumber, replayCharNumber); /* request a character to be printed to the display */
            if(0 != replayCharNumber) /* check if line is complete */
            {
                Timer_Readback_Start(); /* trigger/start readback oneshot timer ... TC interrupt handler will set readBackFlag */
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
    timeoutFlag = true; /* set timeOut flag to indicate escape sequence parsing should be abandoned/aborted */
    Timer_Timeout_STATUS; /* read timer Status to clear "sticky" interrupt bit */
    Timer_Timeout_Stop(); /* stopping the timer reloads the period counter with configuration value */
    isr_timeout_ClearPending(); /* clear the pending interrupt in the isr component */
} 

CY_ISR(uartISR)
{
    /* note - UART_ReadRxStatus() not needed ... UART_RX_STS_FIFO_NOTEMPTY clears immediately after RX data register read. */
    rxFIFO[headPointer++] = UART_GetChar(); /* place received character from UART in FIFO */
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
    
    isr_UART_ClearPending(); /* clear the pending interrupt in the isr component */
} 

CY_ISR(readbackISR)
{
    readBackFlag = true; /* set flag to indicate that a new readback character can be requested */
    Timer_Readback_STATUS; /* read timer Status to clear "sticky" interrupt bit */
    Timer_Readback_Stop(); /* stopping the timer reloads the period counter with configuration value */
    isr_readback_ClearPending(); /* clear the pending interrupt in the isr component */
} 

/* [] END OF FILE */
