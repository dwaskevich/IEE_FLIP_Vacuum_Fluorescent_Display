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

int main(void)
{
    char printBuffer[100];
    char rxData;
    uint8_t entryMode, cursorPosition;
    uint16_t currentLineBufferID = 0;
    uint8_t updateDisplayFlag = FALSE;
        
    CyGlobalIntEnable; /* Enable global interrupts. */
    
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    /* initialize VFD display (returns entry mode defined in .h file) */
    entryMode = VFD_InitializeDisplay(DEFAULT_ENTRY_MODE);
    
//    /* initialize display history */
//    VFD_InitDisplayHistory();
    
    /* initialize and start UART */
    UART_Start();    
    UART_PutString("\r\nUART started ...\r\n");
    
    /* initialize display history */
    sprintf(printBuffer, "Initializing display history. Size = %d\r\n", VFD_InitDisplayHistory());
    UART_PutString(printBuffer);
    
    UART_PutString("writing long string to VFD\r\n");
    uint16_t numCharsWritten = 0;
    numCharsWritten = VFD_PutString("This is a test string to see what happens when it's too long. I guess it really doesn't matter!");
    sprintf(printBuffer, "VFD_PutString return value = %d\r\n", numCharsWritten);
    UART_PutString(printBuffer);
    
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
    }}

/* [] END OF FILE */
