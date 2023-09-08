/* ========================================
 *
 * Name:    iee_flip_03600_20_040.h
 * 
 * Date:    7-Sept-2023
 * Author:  David Waskevich
 *
 * Description: header file IEE Flip VFD driver
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
#include "stdint.h"

/***********************************
 * Macros
 ***********************************/

/* time between successive writes ... somewhat arbitrary ... trial and error ... 750usec would probably be better */
#define WRITE_DELAY (1u)

/* per 8041 data sheet, min WR pulse is 250ns ... GPIO API is slow enough (~575ns measured on oscilloscope) */
#define WR_STROBE(x)  do{ \
            WR_Write(0); \
            WR_Write(1); \
            CyDelay(x); \
} while (0)

/* this seemed to be the only control code (i.e. A0 high) that did anything */
#define CLEAR_DISPLAY() do{ \
            A0_Write(1); \
            DataBus_Write(CLR); \
            WR_STROBE(WRITE_DELAY); \
            A0_Write(0); \
            DataBus_Write(LF); \
            WR_STROBE(WRITE_DELAY); \
} while (0)

/* general 8-bit write ... per 8041 data sheet, data setup time to trailing (rising) edge of /WR is 150ns */
#define DISPLAY_WRITE(x)    do{ \
            DataBus_Write(x); \
            WR_STROBE(WRITE_DELAY); \
} while (0)

/* useful constants */
#define LINE_LENGTH (40u)
#define CR          (0x0d)
#define LF          (0x0a)
#define CLR         (0x00)
#define BS          (0x08)
#define TAB         (0x09)
#define CTRL_G      (0x07)
#define EOL_WRAP    (0x11)
#define EOL_STOP    (0x12)


/***********************************
 * Function prototypes
 ***********************************/

uint16_t VFD_PositionCursor(uint8_t position);
void VFD_PutChar(char value);
void VFD_WriteByte(uint8_t value);
uint16_t VFD_PutString(char *str);
void VFD_ClearDisplay(void);
void VFD_SetEndOfLineWrap(uint8_t mode);


/* [] END OF FILE */
