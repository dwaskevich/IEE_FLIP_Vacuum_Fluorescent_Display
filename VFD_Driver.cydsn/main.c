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
 *          -> One-shot timer generates an interrupt just longer than 115,200
 *             arrival time
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

CY_ISR_PROTO(timerISR);

volatile bool timeoutFlag = false;

/* Escape sequence state machine */
enum escSeqStates
{
    ESCAPE,
    X5B,
    X7E
};
enum escSeqStates escSeqState = ESCAPE;

int main(void)
{
    char printBuffer[100];
    char rxData;
    uint8_t entryMode, cursorPosition;
    uint16_t currentLineBufferID = 0;
    uint8_t updateDisplayFlag = FALSE;
        
    CyGlobalIntEnable; /* Enable global interrupts. */
    
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    /* One-shot timer (distinguishes ESC key from escape sequences */
    Timer_SetInterruptMode(Timer_STATUS_TC_INT_MASK );
    isr_timeout_StartEx(timerISR);
    
    /* initialize VFD display (returns entry mode defined in .h file) */
    entryMode = VFD_InitializeDisplay(DEFAULT_ENTRY_MODE);
    
//    /* initialize display history */
//    VFD_InitDisplayHistory();
    
    /* initialize and start UART */
    UART_Start();    
    UART_PutString("\r\nUART started ...\r\n");
    
    /* initialize display history */
    sprintf(printBuffer, "Initializing display history. Number of pages = %d\r\n", VFD_InitDisplayHistory());
    UART_PutString(printBuffer);
    
    sprintf(printBuffer, "SRAM usage for display history = %d\r\n", VFD_SizeOfHistoryArray());
    UART_PutString(printBuffer);
    
    UART_PutString("writing long string to VFD\r\n");
    uint16_t numCharsWritten = 0;
    numCharsWritten = VFD_PutString("This is a test string to see what happens when it's too long. I guess it really doesn't matter!");
    sprintf(printBuffer, "VFD_PutString return value = %d\r\n", numCharsWritten);
    UART_PutString(printBuffer);
    
    UART_PutString("Testing VFD_PositionCursor() ... 204\r\n");
    sprintf(printBuffer, "Actual cursor position = %d\r\n", VFD_PositionCursor(204));
    UART_PutString(printBuffer);
    
    UserLED_Write(0);
    for(uint8_t i = 0; i < 5; i++)
    {
        UserLED_Write(~UserLED_Read());
        CyDelay(250);
    }
    
    bool isEscapeSequenceFlag = false;
    char escSequence[4] = {0};
    uint8_t escSequenceNum = 0;
    
    while(1)
    {
        uint8_t bufferSize;
        if((bufferSize = UART_GetRxBufferSize()))
        {
            Timer_Stop(); /* stop the ESC timeout timer on each new character received */
            rxData = UART_GetChar();
            if(ESC == rxData) /* ESC key detected ... determine if it's just a key or an escape sequence */
            {
                escSequenceNum = 0;
                escSequence[escSequenceNum++] = rxData; /* save ESC character for later */
                isEscapeSequenceFlag = true;
                Timer_Start(); /* start timeout timer (timeout period set to > 115,200 arrival time) */
            }
            else if(true == isEscapeSequenceFlag)
            {
                switch(escSeqState)
                {
                    case ESCAPE:
                        if(0x5b == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            escSeqState = X5B;
                        }
                        else /* expected character (0x5b) in escape sequence not found ... abandon */
                        {
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                    
                        break;
                    
                    case X5B: /* expected character (0x5b) found ... keep parsing escape sequence (3rd character in sequence) */
                        if(UP_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("UP_ARROW\r\n");
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                        else if(DOWN_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("DOWN_ARROW\r\n");
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                        else if(RIGHT_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("RIGHT_ARROW\r\n");
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                        else if(LEFT_ARROW == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("LEFT_ARROW\r\n");
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                        else if(PAGE_UP == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("PAGE_UP\r\n");
                            escSeqState = X7E;
                        }
                        else if(PAGE_DOWN == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("PAGE_DOWN\r\n");
                            escSeqState = X7E;
                        }
                        else if(HOME == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("HOME\r\n");
                            escSeqState = X7E;
                        }
                        else if(END == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("END\r\n");
                            escSeqState = X7E;
                        }
                        else
                        {
                            UART_PutString("Untracked 4-byte sequence\r\n");
                            escSeqState = X7E;
                        }
                    
                        break;
                    
                    case X7E: /* last (4th) character of escape sequence (0x7e) */
                        if(0x7e == rxData)
                        {
                            escSequence[escSequenceNum++] = rxData;
                            UART_PutString("4-Byte Sequence ... ");
                            for(uint8_t i = 0; i < 4; i++)
                            {
                                sprintf(printBuffer, "%02x ", escSequence[i]);
                                UART_PutString(printBuffer);
                            }
                            UART_PutString("\r\n");
                            isEscapeSequenceFlag = false;
                            escSeqState = ESCAPE;
                        }
                    
                        break;
                    
                    default:
                    
                        break;
                }
            }
            else
            {   
                sprintf(printBuffer, "%02x ", rxData);
                UART_PutString(printBuffer);
            }
        }
        if(true == timeoutFlag) /* ESC key only, not an escape sequence */
        {
            UART_PutString("ESC\r\n");
            isEscapeSequenceFlag = false;
            timeoutFlag = false;
        }
    }
    
    while(1)
    {
        uint8_t bufferSize;
        /* check for incoming characters */
        if((bufferSize = UART_GetRxBufferSize()))
        {
            rxData = UART_GetChar();
            UART_PutChar(rxData);
            
//            sprintf(printBuffer, " %d .. 0x%02X ", bufferSize, rxData);
//            UART_PutString(printBuffer);
            
            /* TODO - replace with switch statement to parse incoming characters */
            if(CR == rxData || LF == rxData) /* handle CR/LF here */
            {
                if(CR == rxData)
                    UART_PutChar(LF);
                    
                if(LF == rxData)
                    UART_PutChar(CR);
                    
                VFD_ClearDisplay(); /* clear display to simulate vertical scroll */
                
                if(RIGHT_ENTRY == entryMode) /* cosmetic (positions underline at end of display) */
                    VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
                
                currentLineBufferID = VFD_CreateNewLine();
                
                sprintf(printBuffer, "\r\nLine Buffer ID = %d\r\n", currentLineBufferID);
                UART_PutString(printBuffer);
            }
//            else if(0x60 == rxData)
            else if('&' == rxData)
            {
                sprintf(printBuffer, "\r\nRecallLine = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                VFD_RecallLine(--currentLineBufferID);
            }
//            else if(0x7e == rxData)
            else if('*' == rxData)
            {
                sprintf(printBuffer, "\r\nReturnHome = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                currentLineBufferID = VFD_ReturnHome();
            }
//            else if(0x7c == rxData)
            else if('$' == rxData)
            {
                sprintf(printBuffer, "\r\nReplayLine = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                VFD_ReplayLine(--currentLineBufferID);
            }
            else if('@' == rxData)
            {
                UART_PutString("writing long string to VFD from forever loop\r\n");
                numCharsWritten = VFD_PutString("This is a test string to see what happens when it's too long. I guess it really doesn't matter!");
                sprintf(printBuffer, "VFD_PutString return value = %d\r\n", numCharsWritten);
                UART_PutString(printBuffer);
            }
            else /* process other characters here */
            {
                currentLineBufferID = VFD_PostToHistory(rxData); /* write to display history */
                updateDisplayFlag = TRUE; /* signal need for display update */
            }
            
            if(CTRL_G == rxData)
            {
                sprintf(printBuffer, "\r\nClearDisplay = 0x%02x\r\n", rxData);
                UART_PutString(printBuffer);
                VFD_ClearDisplay();
            }
        }
        
        if(TRUE == updateDisplayFlag)
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
    timeoutFlag = TRUE;
//    isEscapeSequenceFlag = false;
    UserLED_Write(~UserLED_Read());
    UART_PutString("\r\nfrom isr\r\n");
    Timer_STATUS;
    Timer_Stop();
    isr_timeout_ClearPending();
} 

/* [] END OF FILE */
