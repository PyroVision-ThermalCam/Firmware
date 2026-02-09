/*
 * timeTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the Time Manager component.
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

#ifndef TIME_TYPES_H_
#define TIME_TYPES_H_

#include <esp_err.h>
#include <esp_event.h>

#include <time.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @brief Time Manager events base.
 */
ESP_EVENT_DECLARE_BASE(TIME_EVENTS);

/** @brief Time Manager event IDs.
 */
typedef enum {
    TIME_EVENT_SYNCHRONIZED,        /**< Time synchronized from SNTP */
    TIME_EVENT_SOURCE_CHANGED,      /**< Time source changed (SNTP/RTC/System)
                                         Data is of type struct tm */
} Time_Event_ID_t;

#endif /* TIME_TYPES_H_ */
