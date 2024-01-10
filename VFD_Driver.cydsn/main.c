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
//#include <stdio.h>

#define PRIMARY_ENTRY_MODE  (LEFT_ENTRY)

struct display {
	uint16_t pageID;
    uint16_t characterCount;
	uint8_t inputPosition;
    uint8_t cursorPosition;
    char inputLineBuffer[INPUT_BUFFER_LENGTH + 1];
};

/* declare storage for display pages (limited by available SRAM) */
#define NUMBER_PAGES    (5)
struct display displayHistory[NUMBER_PAGES];

/* declare and assign pointer to display history array */
struct display *ptrDisplay = displayHistory;
/* declare a pointer to the line buffer */
char *ptrLineBuffer;

int main(void)
{
//    char printBuffer[80]; /* print buffer for sprintf() */
    char rxData;
    
    uint8_t updateDisplayFlag = FALSE;
    uint8_t entryMode = PRIMARY_ENTRY_MODE;
        
    CyGlobalIntEnable; /* Enable global interrupts. */
    
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    /* initialize display history */
    for(uint8_t i = 0; i < NUMBER_PAGES; i++)
    {
        ptrDisplay->pageID = i; /* arbitrary identifier */
        ptrDisplay->cursorPosition = 0;
        ptrDisplay->inputPosition = 0;
        ptrDisplay->characterCount = 0;
        ptrDisplay->inputLineBuffer[0] = '\0'; /* null-terminated string */
        ptrDisplay++;
    }    
    ptrDisplay = displayHistory; /* reinitialize display history pointer */
    
    /* initialize and start UART */
    UART_Start();    
    UART_PutString("\r\nUART started ...\r\n");
    
    /* initialize display */
    VFD_EnableDisplay();    
    VFD_ClearDisplay();
    VFD_SetEndOfLineWrap(EOL_STOP);
    
    while(1)
    {
        /* check for incoming characters */
        if(UART_GetRxBufferSize())
        {
            rxData = UART_GetChar();
            UART_PutChar(rxData);
            
            /* TODO - replace with switch statement to parse incoming characters */
            if(CR == rxData || LF == rxData) /* handle CR/LF here */
            {
                if(CR == rxData)
                    UART_PutChar(LF);
                    
                if(LF == rxData)
                    UART_PutChar(CR);
                    
                VFD_ClearDisplay(); /* clear display to simulate vertical scroll */
                
                /* move to next page in circular page buffer */
                ptrDisplay++;
                /* check for circular rollover */
                if(ptrDisplay >= &displayHistory[NUMBER_PAGES])
                {
                    ptrDisplay = displayHistory; /* reset structure pointer to beginning */
                }
                
                /* initialize/tidy next buffer */
                ptrDisplay->inputPosition = 0;
                ptrDisplay->cursorPosition = 0;
                ptrDisplay->characterCount = 0;
                ptrDisplay->inputLineBuffer[0] = '\0';
                
                /* switch back to LEFT_ENTRY if primary entry is LEFT_ENTRY */
                if(PRIMARY_ENTRY_MODE == LEFT_ENTRY)
                    entryMode = LEFT_ENTRY;
            }
            else /* process other characters here */
            {
//                sprintf(printBuffer, "\tline input position = %d\t", ptrDisplay->inputPosition);
//                UART_PutString(printBuffer);
                ptrDisplay->characterCount++; /* increment character count */
                ptrDisplay->inputLineBuffer[ptrDisplay->inputPosition] = rxData; /* store character */
                ptrDisplay->inputLineBuffer[ptrDisplay->inputPosition + 1] = '\0'; /* store character */
                if(ptrDisplay->characterCount < INPUT_BUFFER_LENGTH)
                    ptrDisplay->inputPosition++;
                
                updateDisplayFlag = TRUE;                
            }
            
            if(CTRL_G == rxData)
                VFD_ClearDisplay();
                
//            if(CTRL_Y == rxData)
//            {
//                entryMode = LEFT_ENTRY;
//                sprintf(printBuffer, " %d\t", entryMode);
//                UART_PutString(printBuffer);
//            }
//            
//            if(CTRL_Z == rxData)
//            {
//                entryMode = RIGHT_ENTRY;
//                sprintf(printBuffer, " %d\t", entryMode);
//                UART_PutString(printBuffer);
//            }
        }
        
        if(TRUE == updateDisplayFlag)
        {
            switch(entryMode)
            {
                case LEFT_ENTRY:
                    if(ptrDisplay->characterCount < INPUT_BUFFER_LENGTH)
                        VFD_WriteDisplay(ptrDisplay->inputLineBuffer[ptrDisplay->inputPosition - 1]);
                    else VFD_WriteDisplay(ptrDisplay->inputLineBuffer[ptrDisplay->inputPosition]);
//                    sprintf(printBuffer, "cursor position = %d\r\n", ptrDisplay->cursorPosition);
//                    UART_PutString(printBuffer);
                    if(ptrDisplay->cursorPosition < DISPLAY_LINE_LENGTH - 1)
                        ptrDisplay->cursorPosition++;
                    else entryMode = RIGHT_ENTRY;
                    
                    break;

                case RIGHT_ENTRY:
                    VFD_ClearDisplay();
                    ptrLineBuffer = ptrDisplay->inputLineBuffer;
                    if(ptrDisplay->characterCount >= INPUT_BUFFER_LENGTH)
                        ptrLineBuffer += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH);
                    else ptrLineBuffer += (ptrDisplay->inputPosition - DISPLAY_LINE_LENGTH);
                    VFD_PutString(ptrLineBuffer);
                    
                    break;
            }
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

/* [] END OF FILE */
