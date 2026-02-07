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
 *                      Initializes time management subsystem with RTC as backup time source
 *                      and prepares SNTP synchronization (starts when network connected).
 *  @note               Call this after DevicesManager_Init() for RTC support.
 *                      SNTP starts automatically when network connects.
 *                      Default timezone is UTC - use TimeManager_SetTimezone().
 *  @param p_RTC_Handle Pointer to RTC device handle (NULL if RTC not available)
 *  @return             ESP_OK on success
 *                      ESP_ERR_NO_MEM if memory allocation fails
 *                      ESP_FAIL if initialization fails
 */
esp_err_t TimeManager_Init(void *p_RTC_Handle);

/** @brief          Deinitialize the Time Manager.
 *                  Stops SNTP synchronization and frees all allocated resources.
 *  @note           Time functions become unavailable after this.
 *                  System time reverts to 1970-01-01 00:00:00 UTC.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if cleanup fails
 */
esp_err_t TimeManager_Deinit(void);

/** @brief          Called when network connection is established.
 *                  Starts SNTP synchronization to obtain accurate time from internet.
 *                  Time is automatically synced to system clock and RTC.
 *  @note           This is called automatically by NetworkManager.
 *                  SNTP sync happens periodically (typically every hour).
 *                  First sync may take a few seconds.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if SNTP start fails
 */
esp_err_t TimeManager_OnNetworkConnected(void);

/** @brief          Called when network connection is lost.
 *                  Stops SNTP synchronization and switches to RTC as backup time source.
 *                  System time continues from last synchronized value.
 *  @note           This is called automatically by NetworkManager.
 *                  RTC maintains time accuracy (typical drift: ±20ppm).
 *  @return         ESP_OK on success
 *                  ESP_FAIL if operation fails
 */
esp_err_t TimeManager_OnNetworkDisconnected(void);

/** @brief          Get the current time from best available source.
 *                  Returns current time from the most reliable source available:
 *                  Priority: SNTP (if connected) > RTC > System clock
 *  @note           Time is in configured timezone (default: UTC).
 *                  p_Source helps determine time reliability.
 *  @param p_Time   Pointer to tm structure to store the time
 *  @param p_Source Optional pointer to store the time source used (can be NULL)
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Time is NULL
 *                  ESP_ERR_INVALID_STATE if no time source available
 */
esp_err_t TimeManager_GetTime(struct tm *p_Time, TimeManager_Source_t *p_Source);

/** @brief          Get the current time as UNIX timestamp.
 *                  Returns seconds since 1970-01-01 00:00:00 UTC (UNIX epoch).
 *  @note           Timestamp is always in UTC regardless of timezone setting.
 *                  Useful for time calculations and comparisons.
 *  @param p_Time   Pointer to store the timestamp (time_t)
 *  @param p_Source Optional pointer to store the time source used (can be NULL)
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Time is NULL
 *                  ESP_ERR_INVALID_STATE if no time source available
 */
esp_err_t TimeManager_GetTimestamp(time_t *p_Time, TimeManager_Source_t *p_Source);

/** @brief          Get the time manager status and statistics.
 *                  Returns detailed status including active source, availability of
 *                  SNTP/RTC, last sync times, and sync counters.
 *  @note           Useful for diagnostics and status display.
 *                  Shows which time sources are available and when last synced.
 *  @param p_Status Pointer to TimeManager_Status_t structure to populate
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Status is NULL
 */
esp_err_t TimeManager_GetStatus(TimeManager_Status_t *p_Status);

/** @brief          Force a time synchronization from SNTP.
 *                  Immediately triggers an SNTP query to synchronize system time.
 *                  Only works when network is connected.
 *  @note           Sync happens asynchronously - may take several seconds.
 *                  System time is updated automatically when sync completes.
 *                  RTC is also updated with synchronized time.
 *  @return         ESP_OK if sync request initiated
 *                  ESP_ERR_INVALID_STATE if network not connected
 *                  ESP_FAIL if SNTP not available
 */
esp_err_t TimeManager_ForceSync(void);

/** @brief          Check if time is synchronized and reliable.
 *                  Returns true if time has been synchronized from SNTP or is being
 *                  maintained by RTC. False if only system clock (unreliable).
 *  @note           Use this to determine if timestamps can be trusted.
 *                  Time is reliable after first SNTP sync or RTC read.
 *  @return         true if time is synchronized from SNTP or RTC
 *                  false if time is from unsynchronized system clock
 */
bool TimeManager_IsTimeSynchronized(void);

/** @brief          Format the current time as string.
 *                  Formats current time using strftime() format string. Useful for
 *                  display and logging purposes.
 *  @note           Time is formatted in configured timezone.
 *                  Common formats: ISO 8601: "%Y-%m-%dT%H:%M:%S%z"
 *  @param p_Buffer Buffer to store the formatted time string
 *  @param Size     Buffer size in bytes (including null terminator)
 *  @param Format   Time format string in strftime() format
 *                  Examples: "%Y-%m-%d %H:%M:%S", "%A, %B %d, %Y"
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Buffer or Format is NULL
 *                  ESP_ERR_INVALID_ARG if Size is 0
 *                  ESP_ERR_INVALID_STATE if no time source available
 */
esp_err_t TimeManager_GetTimeString(char *p_Buffer, size_t Size, const char *Format);

/** @brief              Set the timezone for time display.
 *                      Configures the timezone used for local time display. System time
 *                      is stored in UTC internally and converted for display.
 *  @note               Format: std offset [dst [offset],start[/time],end[/time]]
 *                      Changes take effect immediately for all time queries.
 *                      Default timezone is UTC if not set.
 *  @param p_Timezone   Timezone string in POSIX format
 *                      Examples:
 *                      - "UTC+0" or "UTC-0" for UTC
 *                      - "CET-1CEST,M3.5.0,M10.5.0/3" for Central European Time
 *                      - "EST5EDT,M3.2.0,M11.1.0" for US Eastern Time
 *                      - "JST-9" for Japan Standard Time
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Timezone is NULL
 *                      ESP_FAIL if timezone string is invalid
 */
esp_err_t TimeManager_SetTimezone(const char *p_Timezone);

#endif /* TIME_MANAGER_H_ */
