/*
 * vl53l1x.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VL53L1CXV0FY1 Time-of-Flight distance sensor driver definition.
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

#ifndef VL53L1X_H_
#define VL53L1X_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>

#include "../I2C/i2c.h"

/** @brief VL53L1X distance measurement mode.
 *         Short mode offers better ambient light immunity (up to ~1.3 m).
 *         Long mode offers longer range (up to ~4 m) at the cost of ambient immunity.
 */
typedef enum {
    VL53L1X_DISTANCE_SHORT  = 0x00,         /**< Short distance mode: up to ~1.3 m. */
    VL53L1X_DISTANCE_LONG   = 0x01,         /**< Long distance mode: up to ~4 m. */
} VL53L1X_DistanceMode_t;

/** @brief VL53L1X measurement timing budget in milliseconds.
 *         Longer budgets improve accuracy and maximum range at the cost of throughput.
 *         The minimum budget in Long distance mode is 20 ms.
 */
typedef enum {
    VL53L1X_TIMING_15MS    = 15,            /**< 15 ms budget (Short mode only). */
    VL53L1X_TIMING_20MS    = 20,            /**< 20 ms budget. */
    VL53L1X_TIMING_33MS    = 33,            /**< 33 ms budget. */
    VL53L1X_TIMING_50MS    = 50,            /**< 50 ms budget. */
    VL53L1X_TIMING_100MS   = 100,           /**< 100 ms budget (default). */
    VL53L1X_TIMING_200MS   = 200,           /**< 200 ms budget. */
    VL53L1X_TIMING_500MS   = 500,           /**< 500 ms budget. */
} VL53L1X_TimingBudget_t;

/** @brief VL53L1X range status codes returned from VL53L1X_GetRangeStatus().
 */
typedef enum {
    VL53L1X_RANGE_VALID         = 0,        /**< Measurement is valid. */
    VL53L1X_RANGE_SIGMA_FAIL    = 1,        /**< Returned signal sigma is above the allowed limit. */
    VL53L1X_RANGE_SIGNAL_FAIL   = 2,        /**< Returned signal strength is below the allowed limit. */
    VL53L1X_RANGE_OUT_OF_BOUNDS = 4,        /**< Target is detected but at an inconsistent location. */
    VL53L1X_RANGE_WRAP_FAIL     = 7,        /**< Target is detected behind the sensor wrap-around zone. */
    VL53L1X_RANGE_INVALID       = 14,       /**< Target not detected; distance value is not meaningful. */
} VL53L1X_RangeStatus_t;

/** @brief VL53L1X measurement result structure.
 */
typedef struct {
    uint16_t Distance_mm;                   /**< Measured distance in millimeters. */
    VL53L1X_RangeStatus_t Status;           /**< Range status; check before using Distance_mm. */
    uint16_t SignalRateMCPS;                /**< Signal return count rate in MCPS (9.7 fixed-point). */
    uint16_t AmbientRateMCPS;               /**< Ambient count rate in MCPS (9.7 fixed-point). */
    uint16_t EffectiveSPADs;                /**< Number of effective SPADs used for the measurement. */
} VL53L1X_Result_t;

/** @brief VL53L1X driver configuration structure.
 */
typedef struct {
    VL53L1X_DistanceMode_t DistanceMode;    /**< Distance measurement mode. */
    VL53L1X_TimingBudget_t TimingBudget;    /**< Timing budget in milliseconds. */
    uint32_t InterMeasurementMs;            /**< Inter-measurement period in ms (continuous mode only).
                                                 Must be >= TimingBudget. Set to 0 to use TimingBudget value. */
} VL53L1X_Config_t;

/** @brief VL53L1X device instance.
 *         Holds the I2C handle and all sensor state required for continuous ranging.
 *         Managed by the DevicesManager; must not be modified directly by the application.
 */
typedef struct {
    i2c_master_dev_handle_t Handle;         /**< I2C device handle. */
    VL53L1X_DistanceMode_t DistanceMode;    /**< Current distance mode (used by SetTimingBudget). */
    uint16_t OscCalibrateVal;               /**< Oscillator calibration value (read at init). */
    bool Calibrated;                        /**< True after manual calibration on first GetResult(). */
    uint8_t SavedVhvInit;                   /**< Saved VHV_CONFIG__INIT; restored by StopContinuous(). */
    uint8_t SavedVhvTimeout;                /**< Saved VHV-timeout; restored by StopContinuous(). */
} VL53L1X_Dev_t;

/** @brief              Initialize the VL53L1X driver.
 *                      Adds the device to the I2C bus, writes the required ULD initialization
 *                      configuration blob, verifies the model ID, and applies the given config.
 *                      The sensor stays in standby after init; call VL53L1X_StartContinuous() to begin.
 *  @note               XSHUT must be driven HIGH before calling this function.
 *                      Boot time after XSHUT HIGH is at least 1.2 ms.
 *                      If p_Config is NULL, Short distance mode and 100 ms timing budget are used.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Device     Pointer to VL53L1X device instance to initialize
 *  @param p_Config     Optional pointer to configuration structure (NULL = use defaults)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NOT_FOUND if model ID verification fails
 *                      ESP_ERR_INVALID_ARG if 15 ms timing budget is requested in Long distance mode
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_Init(i2c_master_bus_handle_t *p_Bus_Handle, VL53L1X_Dev_t *p_Device,
                       const VL53L1X_Config_t *p_Config = NULL);

/** @brief              Deinitialize the VL53L1X driver and free resources.
 *                      Stops any ongoing ranging and removes the device from the I2C bus.
 *  @note               After calling this function, the device instance is invalid.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_Deinit(VL53L1X_Dev_t *p_Device);

/** @brief              Set the distance measurement mode.
 *                      Updates VCSEL period, timing guard, and phase calibration registers.
 *                      Re-applies the current timing budget after changing distance mode.
 *  @note               The timing budget must be re-applied after changing distance mode.
 *                      15 ms timing budget is not available in Long distance mode.
 *                      Stop ranging before calling this function.
 *  @param p_Device     Pointer to device instance
 *  @param Mode         Desired distance mode
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL or invalid mode
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_SetDistanceMode(VL53L1X_Dev_t *p_Device, VL53L1X_DistanceMode_t Mode);

/** @brief              Set the measurement timing budget.
 *                      Controls the time the sensor spends on each measurement.
 *                      Longer budgets improve accuracy and maximum range.
 *  @note               15 ms is only supported in Short distance mode.
 *                      Stop ranging before calling this function.
 *  @param p_Device     Pointer to device instance
 *  @param Budget       Desired timing budget
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL or 15 ms budget selected in Long mode
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_SetTimingBudget(VL53L1X_Dev_t *p_Device, VL53L1X_TimingBudget_t Budget);

/** @brief              Start continuous ranging mode.
 *                      The sensor performs measurements back-to-back. Each measurement fires
 *                      the GPIO1 interrupt. Use VL53L1X_StartContinuous() instead to control
 *                      the inter-measurement period explicitly.
 *  @note               Call VL53L1X_IsDataReady() to check when a measurement is available.
 *                      Call VL53L1X_ClearInterrupt() after reading each result to allow the next.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_StartRanging(VL53L1X_Dev_t *p_Device);

/** @brief              Trigger a single distance measurement.
 *                      The sensor performs one measurement and returns to standby.
 *  @note               Call VL53L1X_IsDataReady() to poll for completion.
 *                      Call VL53L1X_ClearInterrupt() after reading the result.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_TriggerSingleShot(VL53L1X_Dev_t *p_Device);

/** @brief              Stop continuous ranging mode.
 *                      Sends the stop command and waits for the sensor to become idle.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_StopRanging(VL53L1X_Dev_t *p_Device);

/** @brief              Check whether a new measurement result is available.
 *                      Reads the GPIO__TIO_HV_STATUS register interrupt flag.
 *  @note               Call VL53L1X_ClearInterrupt() after reading the result to re-arm.
 *  @param p_Device     Pointer to device instance
 *  @param p_Ready      Pointer to store ready flag (true if new data is available)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_IsDataReady(VL53L1X_Dev_t *p_Device, bool *p_Ready);

/** @brief              Read the latest measurement result.
 *                      Reads distance, range status, signal rate, ambient rate, and SPAD count.
 *  @note               Call VL53L1X_IsDataReady() before this function.
 *                      Call VL53L1X_ClearInterrupt() after reading to allow the next measurement.
 *                      Always check p_Result->Status before using p_Result->Distance_mm.
 *  @param p_Device     Pointer to device instance
 *  @param p_Result     Pointer to result structure to populate
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_GetResult(VL53L1X_Dev_t *p_Device, VL53L1X_Result_t *p_Result);

/** @brief              Clear the measurement interrupt flag.
 *                      Must be called after reading a result in order to allow the next
 *                      measurement to be signaled.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_ClearInterrupt(VL53L1X_Dev_t *p_Device);

/** @brief              Read the model ID register.
 *                      Expected value for VL53L1CXV0FY1: 0xEA.
 *  @param p_Device     Pointer to device instance
 *  @param p_ModelID    Pointer to store the model ID byte
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_GetModelID(VL53L1X_Dev_t *p_Device, uint8_t *p_ModelID);

/** @brief              Start timed continuous ranging with a configured inter-measurement period.
 *                      Programs SYSTEM__INTERMEASUREMENT_PERIOD from the given period in milliseconds
 *                      (encoded as PeriodMs × osc_calibrate_val as per ST API), clears the interrupt
 *                      flag, and starts the sensor in timed continuous mode (mode_range__timed = 0x40).
 *  @note               VL53L1X_Init() must be called before this function.
 *                      PeriodMs must be at least as large as the configured timing budget.
 *                      Call VL53L1X_IsDataReady() to check when a measurement is available.
 *                      Call VL53L1X_GetResult() and VL53L1X_ClearInterrupt() after each measurement.
 *                      Call VL53L1X_StopContinuous() to stop and restore calibration state.
 *  @param p_Device     Pointer to device instance
 *  @param PeriodMs     Inter-measurement period in milliseconds
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_StartContinuous(VL53L1X_Dev_t *p_Device, uint32_t PeriodMs);

/** @brief              Stop continuous ranging and restore VHV calibration state.
 *                      Sends mode_range__abort (0x80) to immediately halt ranging, resets the
 *                      calibration flag, and restores VHV_CONFIG__INIT and
 *                      VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND to the values saved during the first
 *                      VL53L1X_GetResult() call. Also clears PHASECAL_CONFIG__OVERRIDE.
 *  @note               Use this function to stop ranging started with VL53L1X_StartContinuous().
 *                      VHV restoration is a no-op if VL53L1X_GetResult() was never called after start.
 *                      Do not use this to stop single-shot ranging; use VL53L1X_StopRanging() instead.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t VL53L1X_StopContinuous(VL53L1X_Dev_t *p_Device);

#endif /* VL53L1X_H_ */
