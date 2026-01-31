/*
 * adc.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: ADC driver interface for battery voltage monitoring.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Errors and commissions should be reported to DanielKampert@kampis-elektroecke.de
 */

#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>
#include <stdbool.h>

#include <esp_err.h>

esp_err_t ADC_Init(void);

esp_err_t ADC_Deinit(void);

/** @brief          Read battery voltage and calculate percentage
 *  @param p_Voltage   Pointer to store battery voltage in mV
 *  @param p_Percentage Pointer to store battery percentage (0-100%)
 *  @return         ESP_OK on success
 */
esp_err_t ADC_ReadBattery(int *p_Voltage, uint8_t *p_Percentage);

#endif /* ADC_H_ */