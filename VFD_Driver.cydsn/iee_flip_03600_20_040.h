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
#define FALSE (0)
#define TRUE  (1)

/* time between successive writes ... somewhat arbitrary ... trial and error ... 750usec would probably be better */
#define WRITE_DELAY_MS (1u)

#define ENABLE_DISPLAY  (0u)
#define DISABLE_DISPLAY (1u)

/* define virtual limits for input buffer and display size */
/* physical limit for INPUT_BUFFER_LENGTH depends on available SRAM */
/* physical limit for DISPLAY_LINE_LENGTH depends display (40 in this case) */
/* note - a DISPLAY_LINE_LENGTH less than physical limit creates a virtual "end-of-line" */
#define INPUT_BUFFER_LENGTH (80u)
#define DISPLAY_LINE_LENGTH (40u)

/* useful constants */
#define CR          (0x0d)
#define LF          (0x0a)
#define CLR         (0x00)
#define BS          (0x08)
#define TAB         (0x09)
#define CTRL_G      (0x07)
#define CTRL_Y      (0x19)
#define CTRL_Z      (0x1a)
#define EOL_WRAP    (0x11)
#define EOL_STOP    (0x12)


#define PRIMARY_ENTRY_MODE  (RIGHT_ENTRY)

/* define data structure for a "frame" of screen data */
struct display {
	uint16_t pageID; /* page (or "line number") ID */
    uint16_t characterCount; /* counts input characters (no limit checking, just rolls over) */
	uint8_t inputPosition; /* pointer to next available location in input buffer */
    uint8_t cursorPosition; /* pointer to screen cursor position */
    char inputLineBuffer[INPUT_BUFFER_LENGTH + 1]; /* input line buffer */
};

/* define number of storage pages for display history (limited by available SRAM) */
#define NUMBER_PAGES    (200u)


/* display entry modes (LEFT = Normal, RIGHT = crawl/scroll left) */
enum EntryMode {
    LEFT_ENTRY,
    LEFT_ENTRY_EOL_SCROLL,
    RIGHT_ENTRY    
};


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
uint8_t VFD_SetEntryMode(uint8_t mode);
uint8_t VFD_InitializeDisplay(uint8_t eolMode);
void VFD_InitDisplayHistory(void);
uint16_t VFD_PostCharToHistory(char newData);
uint16_t VFD_CreateNewLine(void);
uint8_t VFD_UpdateDisplay(void);


/* [] END OF FILE */
