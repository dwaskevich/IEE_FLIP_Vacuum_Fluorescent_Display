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

#define WRITE_DELAY (1u)

#define WR_STROBE(x)  do{ \
            WR_Write(0); \
            WR_Write(1); \
            CyDelay(x); \
} while (0)
            
#define CLEAR_DISPLAY() do{ \
            A0_Write(1); \
            DataBus_Write(CLR); \
            WR_STROBE(WRITE_DELAY); \
            A0_Write(0); \
            DataBus_Write(LF); \
            WR_STROBE(WRITE_DELAY); \
            A0_Write(0); \
} while (0)
            
#define DISPLAY_WRITE(x)    do{ \
            DataBus_Write(x); \
            WR_STROBE(WRITE_DELAY); \
} while (0)
            
#define CR  (0x0d)
#define LF  (0x0a)
#define CLR (0x00)
#define BS  (0x08)
#define FS  (0x09)

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
        DISPLAY_WRITE(FS);
        CyDelay(100);
    }
    
    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE('0' + i);
    }
    
    CyDelay(500);
    
    for(uint8_t i = 0; i < 10; i++)
    {
        DISPLAY_WRITE(FS);
        CyDelay(100);
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

    for(;;)
    {
        /* Place your application code here. */
        UserLED_Write(~UserLED_Read());
        
        for(uint8_t i = 0; i < 26; i++)
        {
            DISPLAY_WRITE('a' + i);
        }
        
        CyDelay(1500);

        for(uint8_t i = 0; i < 6; i++)
        {
            DISPLAY_WRITE(FS);
        }
        
        for(uint8_t i = 0; i < 10; i++)
        {
            DISPLAY_WRITE('0' + i);
        }
        
        CyDelay(1500);

        DISPLAY_WRITE(LF);
                
    }
}

/* [] END OF FILE */
