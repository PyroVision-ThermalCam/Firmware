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

/** @brief          Initialize the Devices Manager.
 *                  Initializes all hardware peripherals including I2C and SPI buses,
 *                  port expander, RTC, ADC, and LED driver. Must be called before
 *                  any other DevicesManager functions.
 *  @note           This function must be called after NVS initialization.
 *                  I2C bus speed: 400 kHz, SPI: up to 40 MHz.
 *  @warning        Not thread-safe. Call once from main task during startup.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_FAIL if I2C/SPI initialization fails
 */
esp_err_t DevicesManager_Init(void);

/** @brief          Deinitialize the Devices Manager.
 *                  Cleans up all device handles, removes I2C/SPI devices, and frees
 *                  allocated resources. Should be called during shutdown.
 *  @note           After calling this, DevicesManager_Init() must be called again.
 *  @warning        All device handles become invalid after this call.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if cleanup fails
 */
esp_err_t DevicesManager_Deinit(void);

/** @brief          Get the I2C bus handle for peripheral devices.
 *                  Returns the I2C bus handle used by RTC, Port Expander, and other
 *                  peripheral devices (not the touch controller).
 *  @note           This is the main I2C bus (I2C_NUM_0).
 *                  Bus speed: 400 kHz.
 *  @return         I2C bus handle on success
 *                  NULL if DevicesManager not initialized
 */
i2c_master_bus_handle_t DevicesManager_GetI2CBusHandle(void);

/** @brief          Get the I2C bus handle for touch controller.
 *                  Returns the dedicated I2C bus handle used exclusively by the
 *                  GT911 touch controller.
 *  @note           This is a separate I2C bus (I2C_NUM_1).
 *                  Dedicated bus prevents interference with other peripherals.
 *  @return         I2C bus handle on success
 *                  NULL if DevicesManager not initialized
 */
i2c_master_bus_handle_t DevicesManager_GetTouchI2CBusHandle(void);

/** @brief          Get the SPI host device identifier.
 *                  Returns the SPI host device that is managed by the Devices Manager.
 *                  This host is shared by LCD display, touch controller, and SD card.
 *  @note           Shared SPI bus requires proper CS (Chip Select) management.
 *                  Maximum clock speed depends on connected device (LCD: 40MHz).
 *  @return         SPI host identifier (typically SPI2_HOST)
 */
spi_host_device_t DevicesManager_GetSPIHost(void);

/** @brief              Get the battery voltage and percentage.
 *                      Reads the battery voltage via ADC and calculates the remaining
 *                      charge percentage based on voltage curve. Voltage measurement is
 *                      enabled via port expander GPIO.
 *  @note               ADC is calibrated using eFuse values.
 *                      Battery voltage range: typically 3.0V to 4.2V for Li-Ion.
 *  @warning            Measurement enables battery voltage divider (increases power consumption).
 *  @param p_Voltage    Pointer to store voltage in mV (millivolts)
 *  @param p_Percentage Pointer to store percentage (0-100)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if ADC read fails
 */
esp_err_t DevicesManager_GetBatteryVoltage(int *p_Voltage, uint8_t *p_Percentage);

/** @brief              Get the battery state of charge (SOC) percentage.
 *                      Calculates the battery SOC percentage based on the voltage reading.
 *  @note               ADC is calibrated using eFuse values.
 *                      Battery voltage range: typically 3.0V to 4.2V for Li-Ion.
 *  @warning            Measurement enables battery voltage divider (increases power consumption).
 *  @param p_Percentage Pointer to store percentage (0-100)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if ADC read fails
 */
esp_err_t DevicesManager_GetStateOfCharge(uint8_t *p_Percentage);

/** @brief          Get the RTC device handle (for Time Manager).
 *                  Returns the I2C device handle for the RV8263-C8 Real-Time Clock.
 *                  Used by TimeManager for time synchronization.
 *  @note           RTC provides backup time when network unavailable.
 *                  This function is typically called by TimeManager only.
 *  @param p_Handle Pointer to store the RTC I2C device handle
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Handle is NULL
 *                  ESP_ERR_INVALID_STATE if DevicesManager not initialized
 */
esp_err_t DevicesManager_GetRTCHandle(i2c_master_dev_handle_t *p_Handle);

/** @brief          Get the current time from the RTC.
 *                  Reads the current date and time from the RV8263-C8 Real-Time Clock
 *                  and converts it to a tm structure.
 *  @note           This is a convenience wrapper for TimeManager.
 *                  Time is in local timezone (not UTC).
 *  @param p_Time   Pointer to tm structure to store the time
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Time is NULL
 *                  ESP_ERR_INVALID_STATE if RTC not initialized
 *                  ESP_FAIL if I2C communication fails
 */
esp_err_t DevicesManager_GetTime(struct tm *p_Time);

/** @brief          Set the time on the RTC.
 *  @param p_Time   Pointer to the time to set
 *  @return         ESP_OK when successful
 */
esp_err_t DevicesManager_SetTime(const struct tm *p_Time);

/** @brief              Get the temperature from the TMP117 sensor.
 *  @param p_Temperature Pointer to store the temperature in Celsius
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Temperature is NULL
 *                      ESP_ERR_INVALID_STATE if TMP117 not initialized
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t DevicesManager_GetTemperature(float *p_Temperature);

#endif /* DEVICESMANAGER_H_ */