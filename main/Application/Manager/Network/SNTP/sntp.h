/*
 * sntp.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: SNTP client implementation.
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

#ifndef SNTP_H_
#define SNTP_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

/** @brief SNTP event types (used as event IDs in SNTP_EVENTS base).
 */
typedef enum {
    SNTP_EVENT_SNTP_SYNCED,                     /**< SNTP time synchronization completed
                                                     Data is of type struct timeval */
    SNTP_EVENT_TZ_CHANGED,                      /**< Timezone changed
                                                     Data is of type const char* */
} SNTP_Event_t;

ESP_EVENT_DECLARE_BASE(SNTP_EVENTS);

/** @brief              Initialize SNTP and event handlers.
 *  @param p_Timezone   Timezone string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3")
 *  @param p_Server     NTP server address (e.g. "pool.ntp.org")
 *  @param SyncInterval SNTP sync interval in seconds
 *  @return             ESP_OK on success
 */
esp_err_t SNTP_Init(const char* p_Timezone = "CET-1CEST,M3.5.0,M10.5.0/3", const char* p_Server = "pool.ntp.org", uint32_t SyncInterval = 3600);

/** @brief  Deinitialize SNTP and event handlers.
 *  @return ESP_OK on success
 */
esp_err_t SNTP_Deinit(void);

/** @brief          Initialize SNTP and synchronize time.
 *  @param Retries  Number of retries to get time
 *  @return         ESP_OK on success
 */
esp_err_t SNTP_GetTime(uint8_t Retries);

/** @brief              Set SNTP timezone.
 *  @param p_Timezone   Timezone string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3")
 */
void SNTP_SetTimezone(const char* p_Timezone);

#endif /* SNTP_H_ */