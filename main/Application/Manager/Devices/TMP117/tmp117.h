/*
 * tmp117.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: TMP117 Temperature Sensor driver definition.
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

#ifndef TMP117_H_
#define TMP117_H_

#include <esp_err.h>

#include "../I2C/i2c.h"

/** @brief              Initializes the TMP117 driver.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t TMP117_Init(i2c_master_bus_handle_t *p_Bus_Handle, i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Deinitializes the TMP117 driver.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t TMP117_Deinit(i2c_master_dev_handle_t *p_Dev_Handle);

#endif /* TMP117_H_ */