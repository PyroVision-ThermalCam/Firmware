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

/** @brief Devices Manager events base.
 */
ESP_EVENT_DECLARE_BASE(DEVICES_EVENTS);

/** @brief Devices Manager event identifiers.
 */
enum {
    DEVICES_EVENT_BATTERY_ALERT,    /**< Battery alert state changed. Data: bool (true = alert active). */
    DEVICES_EVENT_BATTERY_CHARGING, /**< Battery charging state changed. Data: bool (true = charging). */
    DEVICES_EVENT_RTC_INTERRUPT,    /**< RTC interrupt asserted. No payload. */
    DEVICES_EVENT_TEMP_INTERRUPT,   /**< Temperature sensor interrupt asserted. No payload. */
    DEVICES_EVENT_RANGE_INTERRUPT,  /**< Range sensor interrupt asserted. No payload. */
    DEVICES_EVENT_SD_DETECT,        /**< SD-card detection state changed. Data: bool (true = card inserted). */
};

/** @brief Backlight identifiers.
 */
typedef enum {
    BACKLIGHT_FLASH = 0,            /**< Flash backlight. */
    BACKLIGHT_DISPLAY,              /**< Display backlight. */
} Devices_BacklightID_t;

#endif /* DEVICES_TYPES_H_ */
