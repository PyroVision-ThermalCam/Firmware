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

#include <stdint.h>
#include <stdbool.h>

#include "../I2C/i2c.h"

/** @brief TMP117 device instance.
 */
typedef struct {
    bool IsInitialized;                 /**< True after successful initialization. */
    i2c_master_dev_handle_t Handle;     /**< I2C device handle. */
} TMP117_Dev_t;

/** @brief TMP117 conversion mode options.
 */
typedef enum {
    TMP117_MODE_CONTINUOUS  = 0x00,     /**< Continuous conversion mode */
    TMP117_MODE_SHUTDOWN    = 0x01,     /**< Shutdown mode (low power) */
    TMP117_MODE_ONE_SHOT    = 0x03,     /**< One-shot conversion mode */
} TMP117_ConversionMode_t;

/** @brief TMP117 conversion cycle time options.
 */
typedef enum {
    TMP117_CYCLE_15_5MS     = 0x00,     /**< 15.5 ms conversion cycle */
    TMP117_CYCLE_125MS      = 0x01,     /**< 125 ms conversion cycle */
    TMP117_CYCLE_250MS      = 0x02,     /**< 250 ms conversion cycle */
    TMP117_CYCLE_500MS      = 0x03,     /**< 500 ms conversion cycle */
    TMP117_CYCLE_1S         = 0x04,     /**< 1 second conversion cycle */
    TMP117_CYCLE_4S         = 0x05,     /**< 4 seconds conversion cycle */
    TMP117_CYCLE_8S         = 0x06,     /**< 8 seconds conversion cycle */
    TMP117_CYCLE_16S        = 0x07,     /**< 16 seconds conversion cycle */
} TMP117_ConversionCycle_t;

/** @brief TMP117 averaging mode options.
 */
typedef enum {
    TMP117_AVG_NONE         = 0x00,     /**< No averaging (1 conversion) */
    TMP117_AVG_8            = 0x01,     /**< Average over 8 conversions */
    TMP117_AVG_32           = 0x02,     /**< Average over 32 conversions */
    TMP117_AVG_64           = 0x03,     /**< Average over 64 conversions */
} TMP117_AveragingMode_t;

/** @brief TMP117 configuration structure.
 */
typedef struct {
    TMP117_ConversionMode_t Mode;       /**< Conversion mode */
    TMP117_ConversionCycle_t Cycle;     /**< Conversion cycle time */
    TMP117_AveragingMode_t Averaging;   /**< Number of conversions to average */
} TMP117_Config_t;

/** @brief              Initializes the TMP117 driver with default configuration.
 *                      Default settings: Continuous mode, 1 second cycle, no averaging.
 *  @note               The device is configured for continuous conversion at 1 second intervals.
 *                      Call TMP117_ReadTemperature() to retrieve temperature readings.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @param p_Config     Optional pointer to configuration structure (if NULL, defaults are used)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NO_MEM if device handle allocation fails
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_Init(i2c_master_bus_handle_t *p_Bus_Handle, TMP117_Dev_t *p_Device,
                      const TMP117_Config_t *p_Config = NULL);

/** @brief              Deinitializes the TMP117 driver and frees resources.
 *  @note               After calling this function, the device instance is invalid.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 */
esp_err_t TMP117_Deinit(TMP117_Dev_t *p_Device);

/** @brief              Configure the TMP117 sensor.
 *                      Allows customization of conversion mode, cycle time, and averaging.
 *  @note               Configuration is written to the device immediately.
 *                      In One-Shot mode, call TMP117_TriggerOneShot() to start conversion.
 *  @param p_Device     Pointer to device instance
 *  @param p_Config     Pointer to configuration structure
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_Configure(TMP117_Dev_t *p_Device, const TMP117_Config_t *p_Config);

/** @brief              Read temperature from TMP117 sensor.
 *                      Reads the current temperature value from the sensor's result register.
 *                      Resolution is 0.0078125°C (7.8125 m°C) per LSB.
 *  @note               In continuous mode, returns the latest conversion result.
 *                      In one-shot mode, returns the result of the last triggered conversion.
 *                      Temperature range: -256°C to +256°C.
 *  @param p_Device     Pointer to device instance
 *  @param p_Temp       Pointer to store temperature in degrees Celsius
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_ReadTemperature(TMP117_Dev_t *p_Device, float *p_Temp);

/** @brief              Trigger a one-shot temperature conversion.
 *                      Only applicable in One-Shot mode. Initiates a single temperature conversion.
 *  @note               Conversion time depends on averaging setting (15.5ms to ~1 second).
 *                      Wait for conversion to complete before reading temperature.
 *                      Use TMP117_IsDataReady() to check if conversion is complete.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_ERR_INVALID_STATE if not in One-Shot mode
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_TriggerOneShot(TMP117_Dev_t *p_Device);

/** @brief              Check if new temperature data is ready.
 *                      Reads the Data_Ready flag from the configuration register.
 *  @note               In continuous mode, flag is set after each conversion completes.
 *                      In one-shot mode, flag is set after the triggered conversion completes.
 *                      Flag is cleared automatically when temperature register is read.
 *  @param p_Device     Pointer to device instance
 *  @param p_Ready      Pointer to store ready status (true if data is ready)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_IsDataReady(TMP117_Dev_t *p_Device, bool *p_Ready);

/** @brief              Read the TMP117 device ID.
 *                      Reads the device ID register to verify sensor identity.
 *                      Expected value: 0x0117
 *  @note               Use this function to verify correct I2C communication.
 *                      Device ID should always read 0x0117.
 *  @param p_Device     Pointer to device instance
 *  @param p_DeviceID   Pointer to store device ID
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t TMP117_ReadDeviceID(TMP117_Dev_t *p_Device, uint16_t *p_DeviceID);

#endif /* TMP117_H_ */