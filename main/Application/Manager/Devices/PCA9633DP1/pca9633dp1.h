/*
 * pca9633dp1.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PCA9633DP1 LED Driver definition.
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

#ifndef PCA9633DP1_H_
#define PCA9633DP1_H_

#include <esp_err.h>

#include "../I2C/i2c.h"

/** @brief LED channel identifier.
 */
typedef enum {
    PCA9633_LED0 = 0,                       /**< LED channel 0. */
    PCA9633_LED1 = 1,                       /**< LED channel 1. */
    PCA9633_LED2 = 2,                       /**< LED channel 2. */
    PCA9633_LED3 = 3,                       /**< LED channel 3. */
} PCA9633_LED_t;

/** @brief LED output state.
 */
typedef enum {
    PCA9633_LED_OFF = 0,                    /**< LED driver off (default power-up state). */
    PCA9633_LED_FULLY_ON = 1,               /**< LED driver fully on (not PWM controlled). */
    PCA9633_LED_PWM = 2,                    /**< LED driver individual PWM + group PWM/dimming. */
    PCA9633_LED_PWM_GRPPWM = 3,             /**< LED driver individual PWM + group blinking. */
} PCA9633_LED_State_t;

/** @brief              Initializes the PCA9633DP1 LED driver.
 *                      Creates I2C device handle and configures the PCA9633 for PWM LED control.
 *                      Default configuration sets all LEDs to off state.
 *  @note               I2C address: 0x62 (default for PCA9633).
 *                      Supports 4 independent PWM channels.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NO_MEM if device handle allocation fails
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_Init(i2c_master_bus_handle_t *p_Bus_Handle, i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Deinitializes the PCA9633DP1 LED driver.
 *                      Removes I2C device handle and frees resources. LEDs are left in
 *                      their current state.
 *  @note               Device handle becomes invalid after this call.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Dev_Handle is NULL
 *                      ESP_FAIL if I2C device removal fails
 */
esp_err_t PCA9633DP1_Deinit(i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Set brightness of a single LED channel.
 *                      Sets the PWM duty cycle for the specified LED channel and enables
 *                      PWM control mode.
 *  @note               LED is automatically enabled in PWM control mode.
 *                      Brightness range: 0 (off) to 255 (full brightness).
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param LED          LED channel (PCA9633_LED0 to PCA9633_LED3)
 *  @param Brightness   PWM duty cycle (0-255)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if device handle NULL or invalid LED
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_SetLED(i2c_master_dev_handle_t *p_Dev_Handle, PCA9633_LED_t LED, uint8_t Brightness);

/** @brief              Set brightness of all LED channels simultaneously.
 *                      Sets PWM duty cycle for all 4 LED channels using auto-increment
 *                      and enables PWM control mode for all channels.
 *  @note               More efficient than calling PCA9633DP1_SetLED() four times.
 *                      All LEDs are automatically enabled in PWM control mode.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param LED0         Brightness for LED channel 0 (0-255)
 *  @param LED1         Brightness for LED channel 1 (0-255)
 *  @param LED2         Brightness for LED channel 2 (0-255)
 *  @param LED3         Brightness for LED channel 3 (0-255)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if device handle is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_SetAllLEDs(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t LED0, uint8_t LED1, uint8_t LED2, uint8_t LED3);

/** @brief              Set output state of an LED channel.
 *                      Controls LED driver output state: off, fully on, PWM controlled,
 *                      or PWM with group control.
 *  @note               Use this to turn LED fully on/off without changing PWM value.
 *                      PWM value is preserved when switching states.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param LED          LED channel (PCA9633_LED0 to PCA9633_LED3)
 *  @param State        Output state (see PCA9633_LED_State_t)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if device handle NULL or invalid LED
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_SetLEDState(i2c_master_dev_handle_t *p_Dev_Handle, PCA9633_LED_t LED, PCA9633_LED_State_t State);

/** @brief              Configure group PWM and blinking control.
 *                      Sets up group control for global dimming or synchronized blinking
 *                      of all LEDs set to group control mode.
 *  @note               Only affects LEDs set to PCA9633_LED_PWM_GRPPWM state.
 *                      Blinking frequency = GroupFreq / 24 Hz (e.g., 255 = 0.17 Hz).
 *                      GroupPWM defines duty cycle in dimming mode or on-time in blink mode.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param GroupPWM     Group PWM duty cycle (0-255)
 *  @param GroupFreq    Group frequency for blinking (0-255), only used when Blinking=true
 *  @param Blinking     true for blinking mode, false for dimming mode
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if device handle is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_SetGroupControl(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t GroupPWM, uint8_t GroupFreq, bool Blinking);

/** @brief              Enable or disable sleep mode (low power mode).
 *                      In sleep mode, the internal oscillator is turned off and PWM
 *                      generation stops. All register contents are preserved.
 *  @note               Sleep mode reduces power consumption significantly.
 *                      Wake-up time: ~500μs for oscillator to stabilize.
 *                      LED outputs are held at their last state during sleep.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param Sleep        true to enter sleep mode, false to wake up
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if device handle is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCA9633DP1_SetSleepMode(i2c_master_dev_handle_t *p_Dev_Handle, bool Sleep);

#endif /* PCA9633DP1_H_ */