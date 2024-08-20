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
 * Hardware:    STM32 Blue Pill (https://predictabledesigns.com/introduction-stm32-blue-pill-stm32duino/)
 *              STM32F103C8T6 CortexM3-based development kit (64K Flash/20K SRAM, 3.3V)
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
	/* abandoned HAL delay function ... 1 ms value actually produces 2 ms delay - see following links */
	/* https://community.st.com/t5/stm32-mcus-embedded-software/hal-delay-1-takes-2ms/td-p/51219 */
	/* http://www.efton.sk/STM32/gotcha/g13.html */
//	HAL_Delay(value);
	uint32_t delayCount = value * STM32_72MHZ_MS_COUNT; /* multiplier determined by trial and error */
	while(delayCount--);
}

/* Note - HAL_GPIO_WritePin API uses GPIOx_BSRR register to allow atomic read/modify access */

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
	HAL_GPIO_WritePin(nRD_GPIO_Port, nRD_Pin, value);
}

void write_A0(uint8_t value)
{
    HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, value);
}

void write_TEST(uint8_t value)
{
	HAL_GPIO_WritePin(nTEST_GPIO_Port, nTEST_Pin, value);
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
