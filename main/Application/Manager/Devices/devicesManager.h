/*
 * devicesManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Devices Manager definition.
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

#ifndef DEVICESMANAGER_H_
#define DEVICESMANAGER_H_

#include <driver/i2c_master.h>
#include <driver/spi_common.h>

#include <time.h>
#include <stdint.h>

#include "I2C/i2c.h"
#include "devicesTypes.h"

/** @brief  Initialize the Devices Manager.
 *  @return ESP_OK on success
 */
esp_err_t DevicesManager_Init(void);

/** @brief  Deinitialize the Devices Manager.
 *  @return ESP_OK on success
 */
esp_err_t DevicesManager_Deinit(void);

/** @brief  Get the I2C bus handle.
 *  @return I2C bus handle or NULL if not initialized
 */
i2c_master_bus_handle_t DevicesManager_GetI2CBusHandle(void);

/** @brief  Get the SPI host device identifier.
 *
 *  Returns the SPI host device (SPI3_HOST) that is managed by the Devices Manager.
 *  This host is shared by LCD, Touch controller, and SD card.
 *
 *  @return SPI3_HOST
 *
 *  @note   The SPI bus must be initialized before use (call DevicesManager_Init() first).
 *  @note   Use SPIM_IsInitialized() to check if the bus is ready.
 */
spi_host_device_t DevicesManager_GetSPIHost(void);

/** @brief              Get the battery voltage and percentage.
 *  @param p_Voltage    Pointer to store voltage in mV
 *  @param p_Percentage Pointer to store percentage (0-100)
 *  @return             ESP_OK on success
 */
esp_err_t DevicesManager_GetBatteryVoltage(int *p_Voltage, uint8_t *p_Percentage);

/** @brief          Get the RTC device handle (for Time Manager).
 *  @param p_Handle Pointer to store the RTC handle
 *  @return         ESP_OK on success
 */
esp_err_t DevicesManager_GetRTCHandle(i2c_master_dev_handle_t *p_Handle);

/** @brief          Get the current time from the RTC.
 *  @param p_Time   Pointer to store the time
 *  @return         ESP_OK when successful
 */
esp_err_t DevicesManager_GetTime(struct tm *p_Time);

/** @brief          Set the time on the RTC.
 *  @param p_Time   Pointer to the time to set
 *  @return         ESP_OK when successful
 */
esp_err_t DevicesManager_SetTime(const struct tm *p_Time);

#endif /* DEVICESMANAGER_H_ */