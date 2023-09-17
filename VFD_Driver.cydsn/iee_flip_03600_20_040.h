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
#define WRITE_DELAY_MS (1u)

#define ENABLE_DISPLAY  (0u)
#define DISABLE_DISPLAY (1u)

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

/* low-level APIs */
void toggleStrobe(uint8_t writeDelay_ms);

/* high-level APIs */
void VFD_EnableDisplay(void);
void VFD_DisableDisplay(void);
void VFD_WriteDisplay(uint8_t value);
uint8_t VFD_ReadDisplay(void);
uint16_t VFD_PositionCursor(uint8_t position);
void VFD_PutChar(char value);
uint16_t VFD_PutString(char *str);
void VFD_ClearDisplay(void);
void VFD_SetEndOfLineWrap(uint8_t mode);
void VFD_Test(uint8_t value);


/* [] END OF FILE */
