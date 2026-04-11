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

#include <freertos/FreeRTOS.h>

#include <time.h>
#include <stdint.h>

#include "I2C/i2c.h"
#include "RV8263C8/rv8263c8.h"
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
 *                  DEVICES_ERR_I2C_BUS_INIT if I2C bus initialization fails
 *                  DEVICES_ERR_SPI_BUS_INIT if SPI bus initialization fails
 *                  DEVICES_ERR_EXPANDER_MAINBOARD if mainboard expander init fails
 *                  DEVICES_ERR_RV8263C8_NOT_FOUND if RTC not detected on I2C bus
 *                  DEVICES_ERR_TMP117_NOT_FOUND if temperature sensor not detected
 *                  DEVICES_ERR_MAX17048_NOT_FOUND if fuel gauge not detected
 *                  DEVICES_ERR_VL53L1X_NOT_FOUND if ranging sensor not detected
 *                  DEVICES_ERR_PCA9633_NOT_FOUND if LED driver not detected
 */
esp_err_t DevicesManager_Init(void);

/** @brief          Deinitialize the Devices Manager.
 *                  Cleans up all device handles, removes I2C/SPI devices, and frees
 *                  allocated resources. Should be called during shutdown.
 *  @note           After calling this, DevicesManager_Init() must be called again.
 *  @warning        All device handles become invalid after this call.
 *  @return         ESP_OK on success
 */
esp_err_t DevicesManager_Deinit(void);

/** @brief          Acquire exclusive access to the shared I2C bus.
 *                  Blocks until the I2C bus mutex is acquired or the timeout expires.
 *                  Must be paired with a call to DevicesManager_ReleaseI2CBus() after the
 *                  multi-transaction operation is complete.
 *  @note           Use this before any multi-step I2C sequence that must not be interleaved
 *                  with other bus users (e.g. Lepton CCI command-response cycles).
 *                  Recommended timeouts: Lepton task — portMAX_DELAY;
 *                  Devices task — pdMS_TO_TICKS(500).
 *  @warning        Always call DevicesManager_ReleaseI2CBus() after a successful acquire,
 *                  even when the I2C operation fails. Failing to do so permanently blocks
 *                  all other I2C users.
 *  @param Timeout  FreeRTOS tick timeout. Use portMAX_DELAY to wait indefinitely.
 *  @return         ESP_OK if the mutex was acquired
 *                  ESP_ERR_TIMEOUT if Timeout expired before the mutex became available
 *                  DEVICES_ERR_NOT_INITIALIZED if DevicesManager is not initialized
 */
esp_err_t DevicesManager_AcquireI2CBus(TickType_t Timeout);

/** @brief          Release exclusive access to the shared I2C bus.
 *                  Releases the mutex previously acquired by DevicesManager_AcquireI2CBus().
 *  @warning        Must only be called after a successful DevicesManager_AcquireI2CBus().
 *                  Calling this without a prior acquire is undefined behavior.
 */
void DevicesManager_ReleaseI2CBus(void);

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
 *  @param p_Charging   Pointer to store the charging state (true = charging in progress)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_ADC_READ if ADC read fails
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetBatteryStatus(int *p_Voltage, uint8_t *p_Percentage, bool *p_Charging);

/** @brief          Get the RTC device handle (for Time Manager).
 *                  Returns the I2C device handle for the RV8263-C8 Real-Time Clock.
 *                  Used by TimeManager for time synchronization.
 *  @note           RTC provides backup time when network unavailable.
 *                  This function is typically called by TimeManager only.
 *  @param p_Handle Pointer to store the RTC device struct
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Handle is NULL
 *                  DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 */
esp_err_t DevicesManager_GetRTCHandle(RV8263C8_Dev_t *p_Handle);

/** @brief          Get the current time from the RTC.
 *                  Reads the current date and time from the RV8263-C8 Real-Time Clock
 *                  and converts it to a tm structure.
 *  @note           This is a convenience wrapper for TimeManager.
 *                  Time is in local timezone (not UTC).
 *  @param p_Time   Pointer to tm structure to store the time
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Time is NULL
 *                  DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                  DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetTime(struct tm *p_Time);

/** @brief          Set the time on the RTC.
 *  @note           Changes are written directly to the RV8263-C8 over I2C.
 *  @param p_Time   Pointer to the time to set
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Time is NULL
 *                  DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                  DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_SetTime(const struct tm *p_Time);

/** @brief                  Get the temperature from the TMP117 sensor.
 *  @param p_Temperature    Pointer to store the temperature in Celsius
 *  @return                 ESP_OK on success
 *                          ESP_ERR_INVALID_ARG if p_Temperature is NULL
 *                          DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                          DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetTemperature(float *p_Temperature);

/** @brief                  Perform a single-shot distance measurement with the VL53L1X sensor.
 *                          Triggers a single measurement, waits for completion (up to 500 ms),
 *                          reads the result, and clears the interrupt. The sensor returns to
 *                          standby after the measurement.
 *  @note                   The measurement duration depends on the configured timing budget
 *                          (default 100 ms). This function blocks the calling task until
 *                          the measurement completes or times out.
 *  @param p_Distance_mm    Pointer to store the measured distance in millimeters
 *  @param p_IsValid        Pointer to store whether the measurement is valid
 *  @return                 ESP_OK on success
 *                          ESP_ERR_INVALID_ARG if any pointer is NULL
 *                          DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                          ESP_ERR_TIMEOUT if measurement did not complete within 500 ms
 *                          DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetDistance(uint16_t *p_Distance_mm, bool *p_IsValid);

/** @brief              Set the brightness of a specific backlight.
 *                      Applies the specified PWM duty cycle to the selected backlight.
 *                      A duty cycle of 0 turns the backlight off (output actively driven LOW by
 *                      totem-pole driver); 255 drives it at full brightness.
 *  @note               Changes take effect immediately.
 *                      This function is thread-safe.
 *  @param ID           Identifier of the backlight to control
 *  @param Brightness   PWM duty cycle for the selected backlight (0 = off, 255 = full brightness)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if ID is out of range
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_SetBrightness(Devices_BacklightID_t ID, uint8_t Brightness);

/** @brief              Enable or disable the Lepton camera reset.
 *                      Controls the reset line of the Lepton thermal camera module via
 *                      the port expander GPIO. Active low reset.
 *  @param Reset        true to assert reset (hold in reset), false to release reset (normal operation)
 *  @return             ESP_OK on success
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_LeptonReset(bool Reset);

/** @brief              Power the Lepton thermal camera on or off (active high, P0.2).
 *  @param Enable       true to power on, false to power off
 *  @return             ESP_OK on success
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_SetLeptonPower(bool Enable);

/** @brief              Assert or release the camera (ESP32-CAM) reset line (active low, P1.6).
 *  @param Reset        true to assert reset (pin driven low), false to release (pin driven high)
 *  @return             ESP_OK on success
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_SetCameraReset(bool Reset);

/** @brief              Power the camera module on or off (active high, P1.7).
 *  @param Enable       true to power on, false to power off
 *  @return             ESP_OK on success
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_EnableCamera(bool Enable);

/** @brief              Read the battery alert state (active low, P0.0).
 *                      Hardware polarity inversion is active; true means the alert is asserted.
 *  @param p_Alert      Pointer to store the alert state (true = alert active)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Alert is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetBatteryAlert(bool *p_Alert);

/** @brief              Read the RTC interrupt state (active low, P0.5).
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Triggered is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetRTCInterrupt(bool *p_Triggered);

/** @brief              Read the temperature sensor interrupt state (active low, P0.7).
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Triggered is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetTempInterrupt(bool *p_Triggered);

/** @brief              Read the range sensor interrupt state (active low, P1.0).
 *  @param p_Triggered  Pointer to store the interrupt state (true = interrupt pending)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Triggered is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetRangeInterrupt(bool *p_Triggered);

/** @brief              Read the SD-card detect state (active low, P1.4).
 *  @param p_Inserted   Pointer to store the detection state (true = card inserted)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Inserted is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetSDDetect(bool *p_Inserted);

/** @brief              Handle a pending port expander interrupt.
 *                      Non-blocking: checks whether the GPIO ISR signalled a new INT# falling
 *                      edge. If so, reads and clears INT_STATUS0/1 from the PCAL6416AHF and
 *                      posts a DEVICES_EVENTS event for every input pin that triggered.
 *  @note               Must be called from a task context (not from an ISR).
 *                      Returns ESP_OK immediately when no interrupt is pending.
 *  @return             ESP_OK on success or when no interrupt is pending
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_HandleExpanderInterrupt(void);

/** @brief              Set the state of the RGB LED.
 *  @param R            Red LED state (true = on, false = off)
 *  @param G            Green LED state (true = on, false = off)
 *  @param B            Blue LED state (true = on, false = off)
 *  @return             ESP_OK on success
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_SetLED(bool R, bool G, bool B);

/** @brief              Read the current state of all displayboard inputs (joystick + buttons).
 *                      Reads the five joystick directions (P0.3–P0.7) and the four push-buttons
 *                      (P1.0–P1.3) from the displayboard PCAL6416AHF. All inputs are active high
 *                      with pull-down resistors; a field is true when the corresponding input is
 *                      pressed.
 *  @note               Returns ESP_ERR_NOT_SUPPORTED if the displayboard expander was not found
 *                      during initialization (displayboard not connected).
 *  @param p_State      Pointer to store the read input state
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_State is NULL
 *                      DEVICES_ERR_NOT_INITIALIZED if DevicesManager not initialized
 *                      ESP_ERR_NOT_SUPPORTED if displayboard not present
 *                      DEVICES_ERR_I2C_COMM if I2C communication fails
 */
esp_err_t DevicesManager_GetDisplayboardInputs(Devices_InputState_t *p_State);

#endif /* DEVICESMANAGER_H_ */