/*
 * devicesTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the Devices Manager component.
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

#ifndef DEVICES_TYPES_H_
#define DEVICES_TYPES_H_

#include <esp_err.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DEVICES_ERR_BASE                    0x1000

/** @defgroup DEVICES_ERRORS Devices Manager Error Codes
 *  @brief Error codes returned by DevicesManager functions (base: @c DEVICES_ERR_BASE = 0x1000).
 *  @{
 */

/** @brief DevicesManager_Init() has not been called yet. */
#define DEVICES_ERR_NOT_INITIALIZED             (DEVICES_ERR_BASE + 0x01)

/** @brief Main I2C bus (SDA/SCL) initialisation failed. */
#define DEVICES_ERR_I2C_BUS_INIT                (DEVICES_ERR_BASE + 0x02)

/** @brief Dedicated touch I2C bus initialisation failed. */
#define DEVICES_ERR_I2C_TOUCH_BUS_INIT          (DEVICES_ERR_BASE + 0x03)

/** @brief SPI bus initialisation failed. */
#define DEVICES_ERR_SPI_BUS_INIT                (DEVICES_ERR_BASE + 0x04)

/** @brief Mainboard PCAL6416AHF port expander (address 0x20) not found on I2C bus (fatal). */
#define DEVICES_ERR_EXPANDER_MAINBOARD          (DEVICES_ERR_BASE + 0x05)

/** @brief Displayboard PCAL6416AHF port expander (address 0x21) not found (non-fatal, displayboard absent). */
#define DEVICES_ERR_EXPANDER_DISPLAYBOARD       (DEVICES_ERR_BASE + 0x06)

/** @brief TMP117 temperature sensor not responding on I2C bus. */
#define DEVICES_ERR_TMP117_NOT_FOUND            (DEVICES_ERR_BASE + 0x07)

/** @brief MAX17048 fuel gauge not responding on I2C bus. */
#define DEVICES_ERR_MAX17048_NOT_FOUND          (DEVICES_ERR_BASE + 0x08)

/** @brief VL53L1X Time-of-Flight range sensor not responding on I2C bus. */
#define DEVICES_ERR_VL53L1X_NOT_FOUND           (DEVICES_ERR_BASE + 0x09)

/** @brief RV8263C8 Real-Time Clock not responding on I2C bus. */
#define DEVICES_ERR_RV8263C8_NOT_FOUND          (DEVICES_ERR_BASE + 0x0A)

/** @brief PCA9633 RGBW LED driver not responding on I2C bus. */
#define DEVICES_ERR_PCA9633_NOT_FOUND           (DEVICES_ERR_BASE + 0x0B)

/** @brief ADC calibration failed (eFuse characterisation data unavailable). */
#define DEVICES_ERR_ADC_CALIBRATION             (DEVICES_ERR_BASE + 0x0C)

/** @brief ADC read failed during battery voltage measurement. */
#define DEVICES_ERR_ADC_READ                    (DEVICES_ERR_BASE + 0x0D)

/** @brief I2C communication error with a peripheral device (NACK, timeout, bus error). */
#define DEVICES_ERR_I2C_COMM                    (DEVICES_ERR_BASE + 0x0E)

/** @brief Requested peripheral was not detected at initialisation and is therefore unavailable. */
#define DEVICES_ERR_DEVICE_NOT_AVAILABLE        (DEVICES_ERR_BASE + 0x0F)

/** @} */

/** @brief Devices Manager events base.
 */
ESP_EVENT_DECLARE_BASE(DEVICES_EVENTS);

/** @brief Devices Manager event identifiers.
 */
enum {
    DEVICES_EVENT_BATTERY_ALERT,                /**< Battery alert state changed.
                                                     Data is of type bool (true = alert active). */
    DEVICES_EVENT_BATTERY_CHARGING,             /**< Battery charging state changed.
                                                     Data is of type bool (true = charging). */
    DEVICES_EVENT_RTC_INTERRUPT,                /**< RTC interrupt asserted. */
    DEVICES_EVENT_TEMP_INTERRUPT,               /**< Temperature sensor interrupt asserted. */
    DEVICES_EVENT_RANGE_INTERRUPT,              /**< Range sensor interrupt asserted. */
    DEVICES_EVENT_SD_DETECT,                    /**< SD-card detection state changed.
                                                     Data is of type bool (true = card inserted). */
    DEVICE_EVENT_RESPONSE_TIME,                 /**< Device RTC time has been updated.
                                                     Data is transmitted in a struct tm structure. */
};

/** @brief Backlight identifiers.
 */
typedef enum {
    BACKLIGHT_FLASH = 0,            /**< Flash backlight. */
    BACKLIGHT_DISPLAY,              /**< Display backlight. */
} Devices_BacklightID_t;

/** @brief Input state of the displayboard controls.
 *         All fields are true when the corresponding button or joystick direction is pressed.
 */
typedef struct {
    bool JoyUp;     /**< Joystick up (P0.3, active high). */
    bool JoyDown;   /**< Joystick down (P0.4, active high). */
    bool JoyLeft;   /**< Joystick left (P0.5, active high). */
    bool JoyRight;  /**< Joystick right (P0.6, active high). */
    bool JoyCenter; /**< Joystick center press (P0.7, active high). */
    bool Button1;   /**< Button 1 (P1.0, active high). */
    bool Button2;   /**< Button 2 (P1.1, active high). */
    bool Button3;   /**< Button 3 (P1.2, active high). */
    bool Button4;   /**< Button 4 (P1.3, active high). */
} Devices_InputState_t;

#endif /* DEVICES_TYPES_H_ */
