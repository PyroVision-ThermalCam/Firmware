/*
 * timeManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Time Manager definition.
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

#ifndef TIME_MANAGER_H_
#define TIME_MANAGER_H_

#include "timeTypes.h"

/** @brief Time source types.
 */
typedef enum {
    TIME_SOURCE_NONE = 0,                   /**< No time source available. */
    TIME_SOURCE_RTC,                        /**< Time from RTC. */
    TIME_SOURCE_SNTP,                       /**< Time from SNTP. */
    TIME_SOURCE_SYSTEM,                     /**< Time from system (not synchronized). */
} TimeManager_Source_t;

/** @brief Time synchronization status.
 */
typedef struct {
    TimeManager_Source_t ActiveSource;      /**< Currently active time source. */
    bool SNTP_Available;                    /**< SNTP available (network connected). */
    bool RTC_Available;                     /**< RTC available. */
    time_t LastSync_SNTP;                   /**< Timestamp of last SNTP sync. */
    time_t LastSync_RTC;                    /**< Timestamp of last RTC sync. */
    uint32_t SNTP_SyncCount;                /**< Number of successful SNTP syncs. */
    uint32_t RTC_SyncCount;                 /**< Number of RTC reads. */
} TimeManager_Status_t;

/** @brief              Initialize the Time Manager.
 *  @param p_RTC_Handle Pointer to RTC device handle (NULL if RTC not available)
 *  @return             ESP_OK on success
 */
esp_err_t TimeManager_Init(void *p_RTC_Handle);

/** @brief  Deinitialize the Time Manager.
 *  @return ESP_OK on success
 */
esp_err_t TimeManager_Deinit(void);

/** @brief  Called when network connection is established.
 *          Starts SNTP synchronization.
 *  @return ESP_OK on success
 */
esp_err_t TimeManager_OnNetworkConnected(void);

/** @brief  Called when network connection is lost.
 *          Switches to RTC as time source.
 *  @return ESP_OK on success
 */
esp_err_t TimeManager_OnNetworkDisconnected(void);

/** @brief          Get the current time (from best available source).
 *  @param p_Time   Pointer to store the time
 *  @param p_Source Optional pointer to store the time source
 *  @return         ESP_OK on success
 */
esp_err_t TimeManager_GetTime(struct tm *p_Time, TimeManager_Source_t *p_Source);

/** @brief          Get the current time as UNIX timestamp.
 *  @param p_Time   Pointer to store the timestamp
 *  @param p_Source Optional pointer to store the time source
 *  @return         ESP_OK on success
 */
esp_err_t TimeManager_GetTimestamp(time_t *p_Time, TimeManager_Source_t *p_Source);

/** @brief          Get the time manager status.
 *  @param p_Status Pointer to store the status
 *  @return         ESP_OK on success
 */
esp_err_t TimeManager_GetStatus(TimeManager_Status_t *p_Status);

/** @brief  Force a time synchronization from SNTP (if available).
 *  @return ESP_OK on success
 */
esp_err_t TimeManager_ForceSync(void);

/** @brief  Check if time is synchronized and reliable.
 *  @return true if time is synchronized from SNTP or RTC
 */
bool TimeManager_IsTimeSynchronized(void);

/** @brief          Format the current time as string.
 *  @param p_Buffer Buffer to store the formatted time
 *  @param Size     Buffer size
 *  @param Format   Time format string (strftime format)
 *  @return         ESP_OK on success
 */
esp_err_t TimeManager_GetTimeString(char *p_Buffer, size_t Size, const char *Format);

/** @brief              Set the timezone for time display.
 *  @param p_Timezone   Timezone string in POSIX format
 *                      Examples:
 *                      - "UTC-0" for UTC
 *                      - "CET-1CEST,M3.5.0,M10.5.0/3" for Central European Time
 *                      - "EST5EDT,M3.2.0,M11.1.0" for US Eastern Time
 *  @return             ESP_OK on success
 */
esp_err_t TimeManager_SetTimezone(const char *p_Timezone);

#endif /* TIME_MANAGER_H_ */
