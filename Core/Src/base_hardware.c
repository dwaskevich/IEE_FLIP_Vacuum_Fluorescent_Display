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

#include "stm32f1xx_ll_gpio.h"
#include "base_hardware.h"

void hw_delay_ms(uint8_t value)
{
	HAL_Delay(value);
}

void write_nWR(uint8_t value)
{
    HAL_GPIO_WritePin(nWR_GPIO_Port, nWR_Pin, value);
}

void write_nCS(uint8_t value)
{
    HAL_GPIO_WritePin(nCS_GPIO_Port, nCS_Pin, value);
}

void write_nRD(uint8_t value)
{
//    RD_Write(value);
}

void write_A0(uint8_t value)
{
    HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, value);
}

void write_TEST(uint8_t value)
{
//    TEST_Write(value);
}

uint8_t read_DataBus(void)
{
//    return DataBus_DR;
    return 0;
}

void write_DataBus(uint8_t value)
{
	uint16_t outputDataRegisterValue;
	outputDataRegisterValue = LL_GPIO_ReadOutputPort(D0_GPIO_Port); /* read port value (16-bits) */
	outputDataRegisterValue &= 0xFF00; /* mask high-order 8 bits and clear lower 8 bits */
	outputDataRegisterValue |= (uint16_t)value; /* over-write lower 8 bits with <value> */
	LL_GPIO_WriteOutputPort(D0_GPIO_Port, outputDataRegisterValue); /* write the port value (16 bits) */
}


/* [] END OF FILE */
