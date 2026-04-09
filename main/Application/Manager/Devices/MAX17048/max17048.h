/*
 * max17048.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: MAX17048 Li-Ion fuel gauge driver definition.
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

#ifndef MAX17048_H_
#define MAX17048_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>

#include "../I2C/i2c.h"

/** @brief MAX17048 device instance.
 */
typedef struct {
    i2c_master_dev_handle_t Handle;     /**< I2C device handle. */
} MAX17048_Dev_t;

/** @brief MAX17048 alert status flag bits (STATUS register 0x1A).
 */
typedef enum {
    MAX17048_ALERT_RESET        = 0x01, /**< Device powered up or reset. */
    MAX17048_ALERT_VOLT_HIGH    = 0x02, /**< VCELL exceeded VALRT.MAX threshold. */
    MAX17048_ALERT_VOLT_LOW     = 0x04, /**< VCELL fell below VALRT.MIN threshold. */
    MAX17048_ALERT_VOLT_RESET   = 0x08, /**< VCELL fell below VRESET threshold. */
    MAX17048_ALERT_SOC_LOW      = 0x10, /**< SOC crossed CONFIG.ATHD threshold. */
    MAX17048_ALERT_SOC_CHANGE   = 0x20, /**< SOC changed by at least 1% (if ALSC is set). */
} MAX17048_AlertFlag_t;

/** @brief              Initialize the MAX17048 fuel gauge driver.
 *                      Adds the device to the I2C bus and verifies communication by reading
 *                      the IC version register. Clears the power-on reset alert flag on success.
 *  @note               The MAX17048 begins ModelGauge operation automatically after power-on.
 *                      No additional configuration is required for basic SOC and voltage readings.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NOT_FOUND if IC version verification fails
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_Init(i2c_master_bus_handle_t *p_Bus_Handle, MAX17048_Dev_t *p_Device);

/** @brief              Deinitialize the MAX17048 driver and free resources.
 *  @note               After calling this function, the device instance is invalid.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C deregistration fails
 */
esp_err_t MAX17048_Deinit(MAX17048_Dev_t *p_Device);

/** @brief              Read the battery cell voltage.
 *                      Reads the 12-bit ADC result from the VCELL register.
 *                      Resolution is 1.25 mV per LSB. Range: 0 V to 5.11 V.
 *  @param p_Device     Pointer to device instance
 *  @param p_Voltage    Pointer to store cell voltage in volts
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_GetVoltage(MAX17048_Dev_t *p_Device, float *p_Voltage);

/** @brief              Read the battery state of charge (SOC).
 *                      Reads the SOC register. Resolution is 1%/256.
 *                      Value is clamped to 0–100%.
 *  @note               SOC is computed by the internal ModelGauge algorithm.
 *                      Call MAX17048_QuickStart() after a fresh battery insertion to
 *                      force immediate SOC re-calculation.
 *  @param p_Device     Pointer to device instance
 *  @param p_SOC        Pointer to store state of charge in percent (0.0–100.0)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_GetSOC(MAX17048_Dev_t *p_Device, float *p_SOC);

/** @brief              Read the battery charge or discharge rate.
 *                      Reads the CRATE register. Positive values indicate charging,
 *                      negative values indicate discharging.
 *                      Resolution is 0.208%/hr per LSB.
 *  @param p_Device     Pointer to device instance
 *  @param p_Rate       Pointer to store charge rate in percent per hour (%/hr)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_GetChargeRate(MAX17048_Dev_t *p_Device, float *p_Rate);

/** @brief              Read the IC production version.
 *                      The top 12 bits of the VERSION register are always 0x001.
 *  @param p_Device     Pointer to device instance
 *  @param p_Version    Pointer to store the 16-bit IC version value
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_GetICVersion(MAX17048_Dev_t *p_Device, uint16_t *p_Version);

/** @brief              Read the alert status flags.
 *                      Returns the current contents of the STATUS register (0x1A).
 *  @param p_Device     Pointer to device instance
 *  @param p_Flags      Pointer to store alert flags (bitmask of MAX17048_AlertFlag_t values)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_GetAlertFlags(MAX17048_Dev_t *p_Device, uint8_t *p_Flags);

/** @brief              Clear one or more alert flags in the STATUS register.
 *  @note               Only bits that are set in Flags will be cleared. Other flags are preserved.
 *  @param p_Device     Pointer to device instance
 *  @param Flags        Bitmask of MAX17048_AlertFlag_t flags to clear
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_ClearAlertFlags(MAX17048_Dev_t *p_Device, uint8_t Flags);

/** @brief              Trigger a quick-start to force immediate SOC re-calculation.
 *                      Restarts the ModelGauge algorithm. Useful after a battery swap
 *                      or when SOC is known to be incorrect.
 *  @note               Quick-start causes a brief (~175 ms) SOC reporting interruption.
 *                      Do not call during normal operation unless SOC is clearly wrong.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t MAX17048_QuickStart(MAX17048_Dev_t *p_Device);

#endif /* MAX17048_H_ */
