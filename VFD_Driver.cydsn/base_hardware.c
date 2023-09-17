/* ========================================
 *
 * File:    base_hardware.c
 * 
 * Date:    16-Sept-2023
 * Author:  David Waskevich
 *
 * Description: Low-level hardware-dependent drivers for 8-bit parallel port
 *              and individual control lines.
 *
 * Usage:       #include "base_hardware.h"
 *
 * Hardware:    Cypress/Infineon PSoC5LP CortexM3 microcontroller and SparkFun
 *              FreeSOC2 Arduino-style kit.
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

void hw_delay_ms(uint8_t value)
{
    CyDelay(value);
}

void write_nWR(uint8_t value)
{
    WR_Write(value);
}

void write_nCS(uint8_t value)
{
    CS_Write(value);
}

void write_nRD(uint8_t value)
{
    RD_Write(value);
}

void write_A0(uint8_t value)
{
    A0_Write(value);
}

void write_TEST(uint8_t value)
{
    TEST_Write(value);
}

uint8_t read_DataBus(void)
{
    return DataBus_DR;
}

void write_DataBus(uint8_t value)
{
    DataBus_DR = value;
}


/* [] END OF FILE */
