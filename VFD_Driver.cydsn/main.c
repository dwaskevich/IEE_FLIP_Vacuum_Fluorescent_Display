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
#include <stdio.h>

char printBuffer[80];
char lineBuffer[80];
uint8_t lineBufferIndex = 0;

int main(void)
{
    CyGlobalIntEnable; /* Enable global interrupts. */

    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    VFD_EnableDisplay();
    
    VFD_ClearDisplay();
    
    for(uint8_t i = 0; i < 26; i++)
    {
        VFD_WriteDisplay('a' + i);
    }
    
    CyDelay(500);
    
    for(uint8_t i = 0; i < 10; i++)
    {
        VFD_WriteDisplay(BS);
        CyDelay(100);
    }
    
     CyDelay(500);

    for(uint8_t i = 0; i < 10; i++)
    {
        VFD_WriteDisplay(TAB);
        CyDelay(100);
    }
    
    for(uint8_t i = 0; i < 10; i++)
    {
        VFD_WriteDisplay('0' + i);
    }
    
    CyDelay(500);
    
    for(uint8_t i = 0; i < 10; i++)
    {
        VFD_WriteDisplay(TAB);
        CyDelay(100);
    }
    
    VFD_WriteDisplay('$');
    VFD_WriteDisplay(CR);
    VFD_WriteDisplay('!');
    
    CyDelay(500);

    for(uint8_t i = 0; i < 10; i++)
    {
        VFD_WriteDisplay(BS);
        CyDelay(100);
    }
    
    CyDelay(500);
    
    VFD_WriteDisplay('*');
    
    CyDelay(1500);
    
    VFD_WriteDisplay(LF);
    
    UART_Start();    
    UART_PutString("UART started ...\r\n");
    
    VFD_PositionCursor(16);
    uint16_t retVal = VFD_PutString("this is a test");
    sprintf(printBuffer, "Return value = %d", retVal);
    UART_PutString(printBuffer);
    
    VFD_PositionCursor(0);
    VFD_PutString("Back home!");
    
    VFD_PositionCursor(33);
    VFD_SetEndOfLineWrap(EOL_STOP);
    VFD_PutString("Overrun end of line");
    
    VFD_SetEndOfLineWrap(EOL_WRAP);
    
    CyDelay(1000);
    
    VFD_PositionCursor(16);
    VFD_ClearToEnd();
    
//    VFD_ClearFromPosition(16);
    
    VFD_PutString("Clearing screen ...");
    CyDelay(2000);
    VFD_ClearDisplay();
    
    VFD_SetEndOfLineWrap(EOL_STOP);
    VFD_PositionCursor(LINE_LENGTH - 1);
    
    lineBuffer[lineBufferIndex++] = 'h';
    lineBuffer[lineBufferIndex++] = 'e';
    lineBuffer[lineBufferIndex++] = 'l';
    lineBuffer[lineBufferIndex++] = 'l';
    lineBuffer[lineBufferIndex++] = 'o';
    
    uint8_t j = lineBufferIndex - 1;
    VFD_PutChar(lineBuffer[j--]);
    do
    {
        VFD_PutChar(BS);
        VFD_PutChar(lineBuffer[j]);
        VFD_PutChar(BS);
    }
    while(0 != j--);
    
    while(1)
    {
        char rxData;
        uint8_t readData;

        if(UART_GetRxBufferSize())
        {
            rxData = UART_GetChar();
            VFD_WriteDisplay(rxData);
            UART_PutChar(rxData);
            sprintf(printBuffer, "--> 0x%02x", rxData);
            UART_PutString(printBuffer);
            readData = VFD_ReadDisplay();
            UART_PutString("\r\nRead data = ");
            UART_PutCRLF(readData);
            if(CTRL_G == rxData)
            {
                VFD_ClearDisplay();
                UART_PutString("\x1b[2J\x1b[;H");
            }
        }
        
        if(0 == User_BTN_Read())
        {
            CyDelay(150);
            VFD_Test(User_BTN_Read());
            while(0 == User_BTN_Read())
            ;
            VFD_Test(1);
        }
    }

    for(;;)
    {
        /* Place your application code here. */
        UserLED_Write(~UserLED_Read());
        
        for(uint8_t i = 0; i < 26; i++)
        {
            VFD_WriteDisplay('a' + i);
        }
        
        CyDelay(1500);

        for(uint8_t i = 0; i < 2; i++)
        {
            VFD_WriteDisplay(TAB);
        }
        
        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay('0' + i);
        }
        
        CyDelay(1500);

        VFD_WriteDisplay(LF);
        
        for(uint8_t i = 0; i < 26; i++)
        {
            VFD_WriteDisplay('a' + i);
            CyDelay(50);
        }
        
        CyDelay(500);
        
        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay(BS);
            CyDelay(50);
        }
        
         CyDelay(500);

        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay(TAB);
            CyDelay(50);
        }
        
        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay('0' + i);
        }
        
        CyDelay(500);
        
        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay(TAB);
            CyDelay(50);
        }
        
        VFD_WriteDisplay('#');
        
        VFD_WriteDisplay(CR);
        VFD_WriteDisplay('!');
        
        CyDelay(500);

        for(uint8_t i = 0; i < 10; i++)
        {
            VFD_WriteDisplay(BS);
            CyDelay(100);
        }
        
        CyDelay(500);
        
        VFD_WriteDisplay('*');
        
        CyDelay(500);
        
        VFD_WriteDisplay(LF);
                
    }
}

/* [] END OF FILE */
