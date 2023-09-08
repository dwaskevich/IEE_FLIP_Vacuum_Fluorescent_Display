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
 *              Note - low-level hardware drivers based on Cypress/Infineon PSoC5LP
 *                  CortexM3 microcontroller and SparkFun FreeSOC2 Arduino-style kit.
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
 *              No control code implementation that I could determine except
 *                  0x00 ... which appears to reset controller.
 *              Read (/RD active) returns value of last character written. Not much value
 *                  so not implemented here.
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

#include "iee_flip_03600_20_040.h"

uint16_t VFD_PositionCursor(uint8_t position)
{
    if(position > LINE_LENGTH)
        return 1;
    DISPLAY_WRITE(CR);
    for(uint8_t i = 0; i < position; i++)
    {
        DISPLAY_WRITE(TAB);        
    }
    return 0;
}

void VFD_PutChar(char value)
{
    DISPLAY_WRITE(value);
}

void VFD_WriteByte(uint8_t value)
{
    DISPLAY_WRITE(value);
}

uint16_t VFD_PutString(char *str)
{
    if('\0' == *str)
        return 0;

    uint8_t i = 0;
    while('\0' != str[i])
    {
        DISPLAY_WRITE(str[i++]);
    }
    return i;
}

void VFD_ClearDisplay(void)
{
    CLEAR_DISPLAY();
}

/* End of line modes are EOL_WRAP & EOL_STOP */
void VFD_SetEndOfLineWrap(uint8_t mode)
{
    DISPLAY_WRITE(mode);
}


/* [] END OF FILE */