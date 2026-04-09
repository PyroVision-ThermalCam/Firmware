/*
 * pcal6416ahf.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PCAL6416AHF Port Expander driver definition.
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

#ifndef PCAL6416AHF_H_
#define PCAL6416AHF_H_

#include <esp_err.h>

#include "../I2C/i2c.h"

/** @brief Port expander device instance.
 */
typedef struct {
    i2c_master_dev_handle_t Handle;     /**< I2C device handle. */
} PCAL6416AHF_Dev_t;

/** @brief              Initializes the port expander driver.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_Init(i2c_master_bus_handle_t *p_Bus_Handle, PCAL6416AHF_Dev_t *p_Device);

/** @brief              Deinitializes the port expander driver.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_Deinit(PCAL6416AHF_Dev_t *p_Device);

/** @brief              Enables or disables the camera power (active high, P1.7).
 *  @param p_Device     Pointer to device instance
 *  @param Enable       true to power on the camera, false to power off
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_EnableCamera(PCAL6416AHF_Dev_t *p_Device, bool Enable);

/** @brief              Enables or disables the LED.
 *  @note               No LED pin is assigned to this port expander. Always returns
 *                      ESP_ERR_NOT_SUPPORTED.
 *  @param p_Device     Pointer to device instance
 *  @param Enable       true to enable the LED, false to disable it
 *  @return             ESP_ERR_NOT_SUPPORTED
 */
esp_err_t PCAL6416AHF_EnableLED(PCAL6416AHF_Dev_t *p_Device, bool Enable);

/** @brief              Controls the Lepton reset line (active low, P1.5).
 *                      The isInverted flag is applied in software: Reset=true drives the pin low.
 *  @param p_Device     Pointer to device instance
 *  @param Reset        true to assert reset (pin low), false to release reset (pin high)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_EnableLeptonReset(PCAL6416AHF_Dev_t *p_Device, bool Reset);

/** @brief              Controls the Lepton power pin (active high, P0.2).
 *  @param p_Device     Pointer to device instance
 *  @param Enable       true to power on, false to power off
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_SetLeptonPower(PCAL6416AHF_Dev_t *p_Device, bool Enable);

/** @brief              Controls the camera reset line (active low, P1.6).
 *                      The isInverted flag is applied in software: Reset=true drives the pin low.
 *  @param p_Device     Pointer to device instance
 *  @param Reset        true to assert reset (pin low), false to release reset (pin high)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_SetCameraReset(PCAL6416AHF_Dev_t *p_Device, bool Reset);

/** @brief              Read the battery alert input (active low, P0.0).
 *                      Hardware polarity inversion is active for this pin, so the returned
 *                      value is already the logical level: true means alert is active.
 *  @param p_Device     Pointer to device instance
 *  @param p_Alert      Pointer to store the alert state (true = alert active)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetBatteryAlert(PCAL6416AHF_Dev_t *p_Device, bool *p_Alert);

/** @brief              Read the battery charging input (active high, P0.1).
 *  @param p_Device     Pointer to device instance
 *  @param p_Charging   Pointer to store the charging state (true = charging)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetBatteryCharging(PCAL6416AHF_Dev_t *p_Device, bool *p_Charging);

/** @brief              Read the RTC interrupt input (active low, P0.5).
 *  @param p_Device     Pointer to device instance
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetRTCInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered);

/** @brief              Read the temperature sensor interrupt input (active low, P0.7).
 *  @param p_Device     Pointer to device instance
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetTempInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered);

/** @brief              Read the range sensor interrupt input (active low, P1.0).
 *  @param p_Device     Pointer to device instance
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetRangeInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered);

/** @brief              Read the SD-card detect input (active low, P1.4).
 *  @param p_Device     Pointer to device instance
 *  @param p_Inserted   Pointer to store the detection state (true = card inserted)
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t PCAL6416AHF_GetSDDetect(PCAL6416AHF_Dev_t *p_Device, bool *p_Inserted);

/** @brief Parsed interrupt status from the PCAL6416AHF INT_STATUS0/1 registers.
 *         One field per interrupt-capable input pin on the mainboard instance.
 *         The *Level / *Inserted fields are derived from the INPUT0/1 registers
 *         that are read atomically (with respect to the latch) inside
 *         PCAL6416AHF_ReadInterruptStatus().  For latched pins (e.g. SD_DETECT)
 *         these reflect the captured pin state at interrupt time, not a later
 *         live read.
 */
typedef struct {
    bool BatteryAlert;          /**< Battery alert pin changed state (P0.0). */
    bool BatteryAlertLevel;     /**< Battery alert level at interrupt time (true = alert active,
                                     hardware polarity-inverted via POL0). */
    bool BatteryCharging;       /**< Battery charging pin changed state (P0.1). */
    bool BatteryChargingLevel;  /**< Battery charging level at interrupt time (true = charging). */
    bool RTCInterrupt;          /**< RTC interrupt pin asserted (P0.5). */
    bool TempInterrupt;         /**< Temperature sensor interrupt pin asserted (P0.7). */
    bool RangeInterrupt;        /**< Range sensor interrupt pin asserted (P1.0). */
    bool SDDetect;              /**< SD-card detect pin changed state (P1.4). */
    bool SDDetectInserted;      /**< SD card present at interrupt time (true = card inserted,
                                     active-low inverted in software). */
} PCAL6416AHF_InterruptStatus_t;

/** @brief              Read and clear both INT_STATUS registers of the PCAL6416AHF.
 *                      Each bit in the hardware registers is set when the corresponding
 *                      input pin changes state (interrupt not masked) and is cleared
 *                      automatically when the register is read.
 *  @param p_Device     Pointer to device instance
 *  @param p_Status     Pointer to store the parsed interrupt status
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Device or p_Status is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCAL6416AHF_ReadInterruptStatus(PCAL6416AHF_Dev_t *p_Device, PCAL6416AHF_InterruptStatus_t *p_Status);

#endif /* PCAL6416AHF_H_ */