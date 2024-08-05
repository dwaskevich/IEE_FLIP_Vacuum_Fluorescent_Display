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
/* not sure why I have the buffer length set to 27u ... must be for testing purposes */
/* yep ... testing purposes. looks like characters start overwriting themselves at 27 */
#define INPUT_BUFFER_LENGTH (27u)
/* note - setting DISPLAY_LINE_LENGTH to (5u) for testing purposes */
#define DISPLAY_LINE_LENGTH (10u)

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
#define ESC         (0x1b)

#define UP_ARROW    (0x41)
#define DOWN_ARROW  (0x42)
#define RIGHT_ARROW (0x43)
#define LEFT_ARROW  (0x44)
#define PAGE_UP     (0x35)
#define PAGE_DOWN   (0x36)
#define HOME        (0x31)
#define END         (0x34)


#define DEFAULT_ENTRY_MODE  (RIGHT_ENTRY)

///* define data structure for a "frame" of screen data */
//struct stc_Display {
//	uint16_t pageID; /* page ID/line number */
//    uint16_t characterCount; /* counts input characters (no limit checking, just rolls over) */
//	uint8_t inputPosition; /* pointer to next available location in input buffer */
//    uint8_t cursorPosition; /* pointer to screen cursor position */
//    char inputLineBuffer[INPUT_BUFFER_LENGTH + 1]; /* input line buffer (with room for '\0' NULL at end) */
//};

/* define number of storage pages for display history (limited by available SRAM) */
#define NUMBER_PAGES    (200u)


/* display entry modes (LEFT = Normal, RIGHT = crawl/scroll left) */
enum EntryMode {
    LEFT_ENTRY,  /* TODO - should this be LEFT_ENTRY_EOL_STOP */
    LEFT_ENTRY_EOL_SCROLL, /*  */
    RIGHT_ENTRY,
    MAX_ENTRY_MODE
};


/***********************************
 * Function prototypes
 ***********************************/

/* low-level APIs */
void toggleStrobe(uint8_t writeDelay_ms);
uint16_t VFD_SizeOfHistoryArray();

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
uint16_t VFD_InitDisplayHistory(void);
uint16_t VFD_PostToHistory(char newData);
uint16_t VFD_CreateNewLine(void);
uint8_t VFD_UpdateDisplay(void);
void VFD_RecallLine(uint16_t lineNumber);
void VFD_ReplayLine(uint16_t lineNumber);
uint16_t VFD_ReturnHome(void);


/* [] END OF FILE */
