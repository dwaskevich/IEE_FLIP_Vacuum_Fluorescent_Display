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

uint8_t cursorPosition = 0;
uint8_t eolWrapMode = EOL_WRAP;
static char lineBuffer[80];
static uint8_t lineBufferIndex = 0;

/* low-level APIs */
void toggleStrobe(uint8_t delay_ms)
{
    /* per 8041 data sheet, min WR pulse is 250ns ... GPIO API is slow enough (~575ns measured on oscilloscope) */
    write_nWR(0);
    write_nWR(1);
    hw_delay_ms(delay_ms);
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
    if(position > LINE_LENGTH)
        return 1;
    cursorPosition = position;
    VFD_WriteDisplay(CR);
    for(uint8_t i = 0; i < position; i++)
    {
        VFD_WriteDisplay(TAB);        
    }
    return 0;
}

void VFD_PutChar(char value)
{
    VFD_WriteDisplay(value);
    cursorPosition++;
}

void VFD_PutCharScroll(char value)
{
    lineBuffer[lineBufferIndex] = value;
    VFD_PositionCursor(LINE_LENGTH - 1);
    VFD_WriteDisplay(lineBuffer[lineBufferIndex]);
    VFD_WriteDisplay(BS);
    for(uint8_t i = 1; i < lineBufferIndex % 40; i++)
    {
        VFD_WriteDisplay(lineBuffer[lineBufferIndex - i]);
        VFD_WriteDisplay(BS);
        VFD_WriteDisplay(BS);
    }
    lineBufferIndex++;
    cursorPosition = LINE_LENGTH - lineBufferIndex;
}

uint16_t VFD_PutString(char *str)
{
    if('\0' == *str)
        return 0;

    uint8_t i = 0;
    while('\0' != str[i])
    {
        VFD_WriteDisplay(str[i++]);
        cursorPosition++;
    }
    return i;
}

void VFD_ClearDisplay(void)
{
    write_A0(1);
    write_DataBus(CLR);
    toggleStrobe(WRITE_DELAY_MS);
    write_A0(0);
    write_DataBus(LF);
    toggleStrobe(WRITE_DELAY_MS);
    cursorPosition = 0;
}

void VFD_ClearToEnd(void)
{
    for(uint8_t i = cursorPosition; i < LINE_LENGTH; i++)
    {
        VFD_WriteDisplay(' ');
    }
    VFD_PositionCursor(cursorPosition);
}

void VFD_ClearFromPosition(uint8_t position)
{
    if(position < LINE_LENGTH)
    {
        VFD_PositionCursor(position);
        for(uint8_t i = position; i < LINE_LENGTH; i++)
        {
            VFD_WriteDisplay(' ');
        }
        VFD_PositionCursor(position);
        cursorPosition = position;
    }
}

/* End of line modes are EOL_WRAP & EOL_STOP */
void VFD_SetEndOfLineWrap(uint8_t mode)
{
    eolWrapMode = mode;
    VFD_WriteDisplay(mode);
}

void VFD_Test(uint8_t value)
{
    write_TEST(value);
}


/* [] END OF FILE */
