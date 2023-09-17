/* ========================================
 *
 * Name:    base_hardware.h
 * 
 * Date:    16-Sept-2023
 * Author:  David Waskevich
 *
 * Description: header file for low-level hardware-dependent interface
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

void hw_delay_ms(uint8_t value);
void write_nWR(uint8_t value);
void write_nCS(uint8_t value);
void write_A0(uint8_t value);
void write_nRD(uint8_t value);
void write_TEST(uint8_t value);
uint8_t read_DataBus(void);
void write_DataBus(uint8_t value);


/* [] END OF FILE */
