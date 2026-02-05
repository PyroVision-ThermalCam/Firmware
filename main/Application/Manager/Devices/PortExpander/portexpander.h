/*
 * portexpander.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GPIO port expander driver interface.
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

#ifndef PORTEXPANDER_H_
#define PORTEXPANDER_H_

#include <esp_err.h>

#include "../I2C/i2c.h"

/** @brief              Initializes the port expander driver.
 *  @param  p_Config    Pointer to the I2C master bus configuration.
 *  @param  Bus_Handle  Pointer to the I2C master bus handle.
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PortExpander_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *Bus_Handle);

/** @brief  Deinitializes the port expander driver.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t PortExpander_Deinit(void);

#ifdef DEBUG
/** @brief Dump the content of the registers from the Port Expander.
 */
void PortExpander_DumpRegister(void);
#endif

/** @brief  Sets the port expander to its default configuration.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t PortExpander_DefaultConfig(void);

/** @brief          Enables or disables the LED.
 *  @param Enable   true to enable the LED, false to disable it.
 *  @return         ESP_OK on success, error code otherwise
 */
esp_err_t PortExpander_EnableLED(bool Enable);

/** @brief 
 *  @param Enable   true to enable battery voltage measurement, false to disable it.
 *  @return         ESP_OK on success, error code otherwise
 */
esp_err_t PortExpander_EnableBatteryVoltage(bool Enable);

#endif /* PORTEXPANDER_H_ */