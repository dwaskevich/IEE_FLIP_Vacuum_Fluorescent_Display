/* ========================================
 *
 * File:    iee_flip_03600_20_040.c
 * 
 * Date:    7-Sept-2023
 * Author:  David Waskevich
 *
 * Description: Driver firmware/library for IEE FLIP 03600-20-040 Vacuum Fluorescent
 *              Display w/8-bit parallel interface.
 *
 * Usage:       #include "iee_flip_03600_20_040.h"
 *              Note - low-level hardware drivers should be implemented in
 *                  base_hardware.c/.h
 *
 * Hardware:    IEE FLIP 03600-20-040 Vacuum Fluorescent Display
 *              Model: 03600-20-040
 *              Mfg date: 8412
 *              5V, 3.5W
 *              PCB markings:
 *                  - Top 05464ASSY 25903-02
 *                  - Bottom 05464-25902-02C
 *              Industrial Electronic Engineers, INC
 *              Van Nuys, CA
 *
 *              Best web link I could find:
 *              https://www.surplusselect.com/products/iee-05464assy-alphanumeric-fluorescent-display-circuit-board-25903-03
 * 
 * Details:     1-line, 40-character alphanumeric display
 *              8-bit parallel interface
 *              6-pin Molex .156" power connector
 *              26-pin dual-row .1" interface header
 *              Intel 8041-based interface
 *
 *              No control code implementation that I could determine except 0x00, which
 *                  appears to reset the controller.
 *              Read (/RD active) returns value of last character written. Not much value
 *                  but implemented here nonetheless.
 *              Carriage Return (CR/0x0D/ctrl-M) returns cursor/current position to beginning
 *                  of line but does not erase existing characters already shown on display.
 *              Line Feed (LF/0x0A/ctrl-J) returns cursor/current position to beginning of
 *                  line and erases display.
 *              Backspace (BS/0x08/ctrl-H) and Tab (HT/0x09/ctrl-I) move cursor/current
 *                  position back and forward one space respectively but do not overwrite the
 *                  existing character.
 *              Line wrap (EOL_WRAP/0x11/ctrl-Q) returns cursor to beginning of line and
 *                  overwrites existing characters.
 *              Disabling Line Wrap (EOL_STOP/0x12/ctrl-R) keeps the cursor at end-of-line and
 *                  overwrites right-most character.
 *              Grounding TEST (pin 1 on interface connector) puts display in "test" mode ...
 *                  displays an ASCII up-count while TEST pin is held low.
 *              Cursor position starts at 0 (not 1)
 *
 * 6-pin Molex power connector:
 *                  Pin #       Function
 *                  -----       --------------
 *                  1           +5V
 *                  2           NC
 *                  3           NC
 *                  4           GND
 *                  5           NC
 *                  6           NC
 *
 * 26-pin IDC header:
 *                  Pin #       Function                Intel 8041 pin #
 *                  -----       --------------          ----------------
 *                  1            TEST                       39
 *                  3           /CS (Chip Select)           6
 *                  5           /RD (Read, not used here)   8
 *                  7            A0 (Command/Data)          9
 *                  9           /WR (Write)                 10
 *                  11-25        D0-D7                      12-19
 *                  2-26         GND
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

#include "base_hardware.h"
#include "iee_flip_03600_20_040.h"

/* define data structure for a "frame" of screen data */
typedef struct{
	uint16_t pageID; /* permanent index */
    uint16_t characterCount; /* counts input characters (no limit checking, just rolls over) */
	uint8_t inputPosition; /* pointer to next available location in input buffer */
    uint8_t cursorPosition; /* pointer to screen cursor position */
    char inputLineBuffer[INPUT_BUFFER_LENGTH + 1]; /* input line buffer (with room for '\0' NULL at end) */
} stc_Display;

volatile uint8_t entryMode = DEFAULT_ENTRY_MODE;
uint16_t currentLine = 0;
stc_Display stc_DisplayHistory[NUMBER_PAGES];

/* declare and assign pointer to display history array */
stc_Display* ptr_stc_Display = stc_DisplayHistory;
/* declare a pointer to the line buffer */
char* ptrLineBuffer;

/* declare and assign pointer to display history array for recall and readback */
stc_Display* ptr_stc_DisplayRecall = stc_DisplayHistory;
/* declare a pointer to the line buffer */
char* ptrLineBufferRecall;


/* low-level APIs */
void toggleStrobe(uint8_t delay_ms)
{
    /* per 8041 data sheet, min WR pulse is 250ns ... GPIO API is slow enough (~575ns measured on oscilloscope) */
    write_nWR(0);
    write_nWR(1);
    hw_delay_ms(delay_ms);
}

uint16_t VFD_SizeOfHistoryArray()
{
    return sizeof(stc_DisplayHistory);
}

/* high-level APIs */
void VFD_EnableDisplay(void)
{
    write_nCS(ENABLE_DISPLAY);
}

void VFD_DisableDisplay(void)
{
    write_nCS(DISABLE_DISPLAY);
}

void VFD_WriteDisplay(uint8_t value)
{
    /* general 8-bit write ... per 8041 data sheet, data setup time to trailing (rising) edge of /WR is 150ns */
    write_DataBus(value);
    toggleStrobe(WRITE_DELAY_MS);
}

uint8_t VFD_ReadDisplay(void)
{
    uint8_t data;
    write_nRD(0);
    hw_delay_ms(5);
    data = read_DataBus();
    write_nRD(1);
    return data;
}

uint16_t VFD_PositionCursor(uint8_t position)
{
    uint8_t i = 0;
    if(position > DISPLAY_LINE_LENGTH) /* check for valid position */
        position = position % DISPLAY_LINE_LENGTH; /* arbitrarily remove integral line lengths */
    VFD_WriteDisplay(CR); /* return cursor to home position */
    for(; i < position; i++)
    {
        VFD_WriteDisplay(TAB); /* TAB character moves cursor forward */
    }
    return i; /* return final position */
}

void VFD_PutChar(char value)
{
    VFD_WriteDisplay(value); /* writes character to current cursor position */
}

uint16_t VFD_PutString(char *str)
{
    if('\0' == *str)
        return 0;

    // TODO - should there be a test for end-of-display to break the while loop?
    uint8_t i = 0;
//    while('\0' != str[i] && i <= INPUT_BUFFER_LENGTH)
    while('\0' != str[i])
    {
        VFD_WriteDisplay(str[i++]); /* write next character from string */
    }
    return i; /* return number of characters written */
}

void VFD_ClearDisplay(void)
{
    write_A0(1);
    write_DataBus(CLR);
    toggleStrobe(WRITE_DELAY_MS);
    write_A0(0);
    write_DataBus(LF);
    toggleStrobe(WRITE_DELAY_MS);
}

/* End of line modes are EOL_WRAP & EOL_STOP */
void VFD_SetEndOfLineWrap(uint8_t mode)
{
    VFD_WriteDisplay(mode);
}

void VFD_Test(uint8_t value)
{
    /* grounding TEST pin puts display in "test" mode ... 
        displays an ASCII up-count while TEST pin is held low. */
    // TODO - this needs to be improved
    write_TEST(value);
}

/* high-level display scrolling APIs */
uint8_t VFD_SetEntryMode(uint8_t mode)
{
    /* check argument for valid mode */
    if(mode < MAX_ENTRY_MODE)
        entryMode = mode;
    else
        entryMode = DEFAULT_ENTRY_MODE;
    
    return entryMode;
}

uint8_t VFD_InitializeDisplay(uint8_t eolMode)
{
    VFD_EnableDisplay();    
    VFD_ClearDisplay();
    if(RIGHT_ENTRY == eolMode)
        VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
    VFD_SetEndOfLineWrap(EOL_STOP);
    
    return VFD_SetEntryMode(entryMode);
}

uint16_t VFD_InitDisplayHistory(void)
{
    /* initialize display history */
    for(uint8_t i = 0; i < NUMBER_PAGES; i++)
    {
        ptr_stc_Display->pageID = i; /* page identifier (permanent index to this array element) */
        ptr_stc_Display->cursorPosition = 0; /* current cursor position for this line */
        ptr_stc_Display->inputPosition = 0; /* index for input buffer to store next incoming character */
        ptr_stc_Display->characterCount = 0; /* running total of characters received */
        ptr_stc_Display->inputLineBuffer[0] = '\0'; /* null-terminated string */
        ptr_stc_Display++; /* move to next array element */
    }    
    ptr_stc_Display = stc_DisplayHistory; /* initialize pointer to first element of display history array */
    
    return sizeof(stc_DisplayHistory) / sizeof(stc_Display); /* confirm/return number of elements initialized */
}

uint16_t VFD_PostToHistory(char newData)
{
    ptr_stc_Display->characterCount++; /* increment character count */
    ptr_stc_Display->inputLineBuffer[ptr_stc_Display->inputPosition] = newData; /* store character */
    ptr_stc_Display->inputLineBuffer[ptr_stc_Display->inputPosition + 1] = '\0'; /* place EOL marker in next location */
    if(ptr_stc_Display->characterCount < INPUT_BUFFER_LENGTH) /* check for/prevent buffer overflow */
        ptr_stc_Display->inputPosition++; /* only increment pointer if below buffer limit */
    
    return ptr_stc_Display->pageID; /* return pageID (permanent index of this buffer/structure element) to caller */
}

uint16_t VFD_CreateNewLine(void)
{
    /* move to next page in circular page buffer */
    ptr_stc_Display++;
    /* check for circular rollover */
    if(ptr_stc_Display >= &stc_DisplayHistory[NUMBER_PAGES])
    {
        ptr_stc_Display = stc_DisplayHistory; /* reset structure pointer to beginning */
    }
    
    /* initialize/tidy new/next buffer */
    ptr_stc_Display->inputPosition = 0;
    ptr_stc_Display->cursorPosition = 0;
    ptr_stc_Display->characterCount = 0;
    ptr_stc_Display->inputLineBuffer[0] = '\0'; /* place string EOL null character */
    
    /* switch back to LEFT_ENTRY if default entry is LEFT_ENTRY */
    if(DEFAULT_ENTRY_MODE == LEFT_ENTRY)
        entryMode = LEFT_ENTRY;
    
    currentLine = ptr_stc_Display->pageID; /* set currentLine to new array index */
    return ptr_stc_Display->pageID;
}

uint8_t VFD_UpdateDisplay(void)
{
    switch(entryMode)
    {
    case LEFT_ENTRY: /* characters enter from left until DISPLAY_LINE_LENGTH, then mode changes to LEFT_ENTRY_EOL_SCROLL */
        if(ptr_stc_Display->characterCount < INPUT_BUFFER_LENGTH) /* check for room in buffer  */
            VFD_WriteDisplay(ptr_stc_Display->inputLineBuffer[ptr_stc_Display->inputPosition - 1]);
        else VFD_WriteDisplay(ptr_stc_Display->inputLineBuffer[ptr_stc_Display->inputPosition]);
        if(ptr_stc_Display->cursorPosition < DISPLAY_LINE_LENGTH - 1)
            ptr_stc_Display->cursorPosition++;
        else
        {
            entryMode = LEFT_ENTRY_EOL_SCROLL; /* reached right end, move to scrolling mode */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        
        break;

    case LEFT_ENTRY_EOL_SCROLL: /* cosmetic transition to crawl (i.e. scroll) once EOL is reached */
        VFD_ClearDisplay();
        ptrLineBuffer = ptr_stc_Display->inputLineBuffer;
        if(ptr_stc_Display->characterCount >= INPUT_BUFFER_LENGTH)
            ptrLineBuffer += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH);
        else ptrLineBuffer += (ptr_stc_Display->inputPosition - DISPLAY_LINE_LENGTH);
        VFD_PutString(ptrLineBuffer);
        VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        
        break;
        
    case RIGHT_ENTRY: /* characters first appear at right-most position, then scroll left */
        ptrLineBuffer = ptr_stc_Display->inputLineBuffer; /* set pointer to beginning of active line buffer */
        if(ptr_stc_Display->characterCount >= INPUT_BUFFER_LENGTH) /* input characters are overruning buffer, overwrite last character */
        {
            ptrLineBuffer += (INPUT_BUFFER_LENGTH - 1); /* adjust line buffer pointer to most recent character */
            VFD_PositionCursor((DISPLAY_LINE_LENGTH - 1)); /* position cursor at end of line */
            VFD_PutChar(*ptrLineBuffer); /* write new character */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        else if(ptr_stc_Display->characterCount > DISPLAY_LINE_LENGTH) /* need to scroll, have to rewrite entire display */
        {
            VFD_ClearDisplay(); /* clear the display to start fresh (cursor returned home) */
            ptrLineBuffer += (ptr_stc_Display->characterCount - DISPLAY_LINE_LENGTH); /* adjust buffer pointer back one display length */
            VFD_PutString(ptrLineBuffer); /* write entire string to overwrite display */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        else /* partial line/display update */
        {
            VFD_PositionCursor((DISPLAY_LINE_LENGTH - 1) - ptr_stc_Display->cursorPosition); /* posiiton cursor back as far as input stream */
            VFD_PutString(ptrLineBuffer); /* overwrite display with partial-line string */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        
        if(ptr_stc_Display->cursorPosition < (DISPLAY_LINE_LENGTH - 1))
            ptr_stc_Display->cursorPosition++; /* update current cursor position if not already at EOL */
        
        break;
        
    default:
        break;
    }
    
    return ptr_stc_Display->cursorPosition;
}

void VFD_RecallLine(uint16_t lineNumber)
{
    if(lineNumber > NUMBER_PAGES)
        return;
    
    ptr_stc_DisplayRecall = stc_DisplayHistory;
    ptr_stc_DisplayRecall += lineNumber; /* move pointer to requested line */
    ptrLineBufferRecall = ptr_stc_DisplayRecall->inputLineBuffer;
    VFD_ClearDisplay();

    switch(entryMode)
    {
        case LEFT_ENTRY:
        if(ptr_stc_DisplayRecall->characterCount >= INPUT_BUFFER_LENGTH)
        {
            ptrLineBufferRecall += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH);
            VFD_PutString(ptrLineBufferRecall);
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        else if(ptr_stc_DisplayRecall->characterCount >= DISPLAY_LINE_LENGTH)
        {
            ptrLineBufferRecall += (ptr_stc_DisplayRecall->characterCount - DISPLAY_LINE_LENGTH);
            VFD_PutString(ptrLineBufferRecall);
            VFD_PositionCursor(DISPLAY_LINE_LENGTH - 1);
        }
        else
        {
            VFD_PutString(ptrLineBufferRecall);
            VFD_PositionCursor(ptr_stc_DisplayRecall->cursorPosition);
        }
        
        break;
        
    case RIGHT_ENTRY:
        if(ptr_stc_DisplayRecall->characterCount >= INPUT_BUFFER_LENGTH) /* input characters are overruning buffer, overwrite last character */
        {
            ptrLineBufferRecall += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH); /* adjust line buffer pointer to most recent character */
            VFD_PutString(ptrLineBufferRecall);
            VFD_PositionCursor(DISPLAY_LINE_LENGTH);
        }
        else if(ptr_stc_DisplayRecall->characterCount > DISPLAY_LINE_LENGTH) /* need to scroll, have to rewrite entire display */
        {
            ptrLineBufferRecall += (ptr_stc_DisplayRecall->characterCount - DISPLAY_LINE_LENGTH); /* adjust buffer pointer back one display length */
            VFD_PutString(ptrLineBufferRecall); /* write entire string to overwrite display */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH);
        }
        else /* partial line/display update */
        {
            VFD_PositionCursor((DISPLAY_LINE_LENGTH) - ptr_stc_DisplayRecall->cursorPosition); /* posiiton cursor back as far as input stream */
            VFD_PutString(ptrLineBufferRecall); /* overwrite display with partial-line string */
            VFD_PositionCursor(DISPLAY_LINE_LENGTH);
        }
        
        break;
        
    default:
        break;
    }
}

void VFD_ReplayLine(uint16_t lineNumber)
{
    uint16_t length = 0;
    
    if(lineNumber > NUMBER_PAGES)
        return;
    
    ptr_stc_DisplayRecall = stc_DisplayHistory;
    ptr_stc_DisplayRecall += lineNumber; /* move pointer to requested line */
    ptrLineBufferRecall = ptr_stc_DisplayRecall->inputLineBuffer;
    VFD_ClearDisplay();
    
    if(ptr_stc_DisplayRecall->characterCount >= INPUT_BUFFER_LENGTH)
        length = INPUT_BUFFER_LENGTH;
    else
        length = ptr_stc_DisplayRecall->characterCount;
    
    switch(entryMode)
    {
    case LEFT_ENTRY:
        for(uint16_t i = 0; i < length; i++)
        {
            if(i < DISPLAY_LINE_LENGTH)
            {
                VFD_PutChar(ptrLineBufferRecall[i]);
                CyDelay(READBACK_SCROLL_DELAY_MS); // TODO - create a delay function, #define for delay
            }
            else
            {
                VFD_ClearDisplay();
                for(int8_t j = DISPLAY_LINE_LENGTH - 1; j >= 0; j--)
                {
                    VFD_PutChar(ptrLineBufferRecall[i - j]);
                }
                CyDelay(READBACK_SCROLL_DELAY_MS);
            }
        }
                
        break;

    case RIGHT_ENTRY:
        for(uint16_t i = 0; i < length; i++)
        {
            for(uint8_t j = 0; j <= i; j++)
            {
                VFD_PositionCursor((DISPLAY_LINE_LENGTH - 1) - j);
                VFD_PutChar(ptrLineBufferRecall[i - j]);
            }
            CyDelay(READBACK_SCROLL_DELAY_MS);

            VFD_PositionCursor(DISPLAY_LINE_LENGTH);
        }
        
        break;
        
    default:
        break;
    }    
}

uint16_t VFD_ReturnHome(void)
{
    VFD_RecallLine(currentLine);
    
#if(0)
    ptr_stc_Display = stc_DisplayHistory;
    ptr_stc_Display += currentLine;
    ptrLineBuffer = ptr_stc_Display->inputLineBuffer;
    VFD_ClearDisplay();
    
    switch(entryMode)
    {
    case LEFT_ENTRY:
        if(ptr_stc_Display->characterCount >= INPUT_BUFFER_LENGTH)
        {
            ptrLineBuffer += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH);
        }
        else if(ptr_stc_Display->characterCount > DISPLAY_LINE_LENGTH)
        {
            ptrLineBuffer += (ptr_stc_Display->characterCount % DISPLAY_LINE_LENGTH);
        }

        VFD_PutString(ptrLineBuffer);
                
        break;

    case LEFT_ENTRY_EOL_SCROLL:
        if(ptr_stc_Display->characterCount >= INPUT_BUFFER_LENGTH)
        {
            ptrLineBuffer += (INPUT_BUFFER_LENGTH - DISPLAY_LINE_LENGTH);
        }
        else if(ptr_stc_Display->characterCount > DISPLAY_LINE_LENGTH)
        {
            ptrLineBuffer += (ptr_stc_Display->characterCount % DISPLAY_LINE_LENGTH);
        }

        VFD_PutString(ptrLineBuffer);
        
        break;
        
    case RIGHT_ENTRY:
        ptrLineBuffer = ptr_stc_Display->inputLineBuffer; /* set pointer to beginning of active line buffer */
        if(ptr_stc_Display->characterCount >= INPUT_BUFFER_LENGTH) /* input characters are overruning buffer, overwrite last character */
        {
            ptrLineBuffer += (INPUT_BUFFER_LENGTH - 1); /* adjust line buffer pointer to most recent character */
            VFD_PositionCursor((DISPLAY_LINE_LENGTH - 1)); /* position cursor at end of line */
            VFD_PutChar(*ptrLineBuffer); /* write new character */
        }
        else if(ptr_stc_Display->characterCount > DISPLAY_LINE_LENGTH) /* need to scroll, have to rewrite entire display */
        {
            VFD_ClearDisplay(); /* clear the display to start fresh (cursor returned home) */
            ptrLineBuffer += (ptr_stc_Display->characterCount - DISPLAY_LINE_LENGTH); /* adjust buffer pointer back one display length */
            VFD_PutString(ptrLineBuffer); /* write entire string to overwrite display */
        }
        else /* partial line/display update */
        {
            VFD_PositionCursor((DISPLAY_LINE_LENGTH - 1) - ptr_stc_Display->cursorPosition); /* posiiton cursor back as far as input stream */
            VFD_PutString(ptrLineBuffer); /* overwrite display with partial-line string */
        }
        
        if(ptr_stc_Display->cursorPosition < (DISPLAY_LINE_LENGTH - 1))
            ptr_stc_Display->cursorPosition++; /* update current cursor position if not already at EOL */
        
        break;
        
    default:
        break;
    }
#endif
    
    return currentLine;
}

uint16_t VFD_GoToOldest(void)
{
    uint16_t oldestLineNumber = currentLine; /* start search at current line */
    ptr_stc_DisplayRecall = stc_DisplayHistory; /* set pointer to beginning of history array */
    ptr_stc_DisplayRecall += currentLine + 1; /* move pointer to one past current line */
    
    /* in a circular buffer, the oldest record will be the next one if the history is full, otherwise search forward for the first non-zero element */
    while(0 == ptr_stc_DisplayRecall->inputLineBuffer[0]) /* look for non-NULL character in first line position */
    {
        /* handle rollover */
        if(ptr_stc_DisplayRecall == &stc_DisplayHistory[NUMBER_PAGES])
        {
            ptr_stc_DisplayRecall = stc_DisplayHistory; /* return pointer back to beginning of array */
            oldestLineNumber = 0;
        }
        else /* otherwise, move pointer forward and increment oldestLineNumber to track/follow */
        {
            ptr_stc_DisplayRecall++;
            oldestLineNumber++;
        }
    }
    
    VFD_RecallLine(oldestLineNumber);
    
    return oldestLineNumber;
}

/* [] END OF FILE */
