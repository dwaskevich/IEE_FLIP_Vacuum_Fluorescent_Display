/* ========================================
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

int main(void)
{
    CyGlobalIntEnable; /* Enable global interrupts. */

    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    CS_Write(1);
    A0_Write(0);
    WR_Write(1);
    
    CS_Write(0);

    for(;;)
    {
        /* Place your application code here. */
        UserLED_Write(~UserLED_Read());
        
        for(uint8_t i = 0; i < 26; i++)
        {
            DataBus_Write('a' + i);
            WR_Write(0);
            WR_Write(1);
            
            CyDelay(1);
        }
        
        CyDelay(500);
        
        A0_Write(1);
        DataBus_Write(0x15);
        WR_Write(0);
        WR_Write(1);
        A0_Write(0);
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DataBus_Write('0' + i);
            WR_Write(0);
            WR_Write(1);
            
            CyDelay(1);
        }
        
        CyDelay(500);
        
        A0_Write(1);
        DataBus_Write(0x15);
        WR_Write(0);
        WR_Write(1);
        A0_Write(0);
        
    }
}

/* [] END OF FILE */
