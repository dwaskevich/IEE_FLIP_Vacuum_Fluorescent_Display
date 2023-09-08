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

int main(void)
{
    CyGlobalIntEnable; /* Enable global interrupts. */

    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    CS_Write(0);
    
    CLEAR_DISPLAY();
    
    for(uint8_t i = 0; i < 26; i++)
    {
        DISPLAY_WRITE('a' + i);
    }
    
    CyDelay(500);
    
    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE(BS);
        CyDelay(100);
    }
    
     CyDelay(500);

    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE(TAB);
        CyDelay(100);
    }
    
    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE('0' + i);
    }
    
    CyDelay(500);
    
    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE(TAB);
        CyDelay(100);
    }
    
    DISPLAY_WRITE('$');
    DISPLAY_WRITE(CR);
    DISPLAY_WRITE('!');
    
    CyDelay(500);

    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE(BS);
        CyDelay(100);
    }
    
    CyDelay(500);
    
    DISPLAY_WRITE('*');
    
    CyDelay(1500);
    
    DISPLAY_WRITE(LF);
    
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
    
    while(1)
    {
        char rxData;
        uint8_t readData;

        if(UART_GetRxBufferSize())
        {
            rxData = UART_GetChar();
            DISPLAY_WRITE(rxData);
            UART_PutChar(rxData);
            sprintf(printBuffer, " (0x%02x)", rxData);
            UART_PutString(printBuffer);
            RD_Write(0);
            CyDelay(5);
            readData = DataBus_Read();
            RD_Write(1);
            UART_PutString("\r\nRead data = ");
            UART_PutCRLF(readData);
            if(CTRL_G == rxData)
                CLEAR_DISPLAY();
        }
        
        if(0 == User_BTN_Read())
        {
            CyDelay(150);
            TEST_Write(User_BTN_Read());
            while(0 == User_BTN_Read())
            ;
            TEST_Write(1);
        }
    }

    for(;;)
    {
        /* Place your application code here. */
        UserLED_Write(~UserLED_Read());
        
        for(uint8_t i = 0; i < 26; i++)
        {
            DISPLAY_WRITE('a' + i);
        }
        
        CyDelay(1500);

        for(uint8_t i = 0; i < 2; i++)
        {
            DISPLAY_WRITE(TAB);
        }
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE('0' + i);
        }
        
        CyDelay(1500);

        DISPLAY_WRITE(LF);
        
        for(uint8_t i = 0; i < 26; i++)
        {
            DISPLAY_WRITE('a' + i);
            CyDelay(50);
        }
        
        CyDelay(500);
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE(BS);
            CyDelay(50);
        }
        
         CyDelay(500);

        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE(TAB);
            CyDelay(50);
        }
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE('0' + i);
        }
        
        CyDelay(500);
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE(TAB);
            CyDelay(50);
        }
        
        DISPLAY_WRITE('#');
        
        DISPLAY_WRITE(CR);
        DISPLAY_WRITE('!');
        
        CyDelay(500);

        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE(BS);
            CyDelay(100);
        }
        
        CyDelay(500);
        
        DISPLAY_WRITE('*');
        
        CyDelay(500);
        
        DISPLAY_WRITE(LF);
                
    }
}

/* [] END OF FILE */
