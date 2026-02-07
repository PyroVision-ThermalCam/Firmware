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

/** @brief          Initializes the ADC driver for battery monitoring.
 *                  Configures ADC channel for battery voltage measurement with
 *                  calibration from eFuse. Uses internal voltage divider.
 *  @note           ADC is calibrated using factory eFuse values.
 *                  Battery voltage is measured through voltage divider.
 *                  Call this after GPIO initialization.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_FAIL if ADC initialization fails
 */
esp_err_t ADC_Init(void);

/** @brief          Deinitializes the ADC driver.
 *                  Frees ADC resources and disables battery voltage monitoring.
 *  @note           ADC readings become unavailable after this.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if ADC cleanup fails
 */
esp_err_t ADC_Deinit(void);

/** @brief              Read battery voltage and calculate percentage.
 *                      Performs ADC reading, applies calibration, and calculates remaining
 *                      battery percentage based on voltage curve for Li-Ion batteries.
 *  @note               Voltage range: typically 3000mV (0%) to 4200mV (100%).
 *                      Percentage uses standard Li-Ion discharge curve.
 *                      Multiple samples are averaged for stability.
 *  @param p_Voltage    Pointer to store battery voltage in millivolts (mV)
 *  @param p_Percentage Pointer to store battery percentage (0-100%)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_INVALID_STATE if ADC not initialized
 *                      ESP_FAIL if ADC read fails
 */
esp_err_t ADC_ReadBattery(int *p_Voltage, uint8_t *p_Percentage);

#endif /* ADC_H_ */