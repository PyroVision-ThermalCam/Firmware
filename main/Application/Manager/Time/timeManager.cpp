/*
 * timeManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Time Manager implementation.
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

#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <sys/time.h>

#include <string.h>

#include "timeManager.h"
#include "../Devices/devicesManager.h"

ESP_EVENT_DEFINE_BASE(TIME_EVENTS);

#define TIME_MANAGER_SYNC_INTERVAL_SEC          3600    /* Sync every hour */
#define TIME_MANAGER_RTC_BACKUP_INTERVAL_SEC    300     /* Save to RTC every 5 minutes */
#define TIME_MANAGER_VALID_YEAR_MIN             2025    /* Minimum valid year */

typedef struct {
    bool isInitialized;
    bool hasRTC;
    bool hasNetwork;
    bool timeSynchronized;
    TimeManager_Source_t activeSource;
    time_t lastSNTP_Sync;
    time_t lastRTC_Sync;
    time_t lastRTC_Backup;
    uint32_t sntpSyncCount;
    uint32_t rtcSyncCount;
    esp_timer_handle_t syncTimer;
} TimeManager_State_t;

static TimeManager_State_t _TimeManager_State;

static const char *TAG = "Time-Manager";

/** @brief  SNTP time synchronization notification callback.
 *  @param tv Pointer to the synchronized time value
 */
static void TimeManager_SNTP_Sync_Callback(struct timeval *tv)
{
    time_t Now = tv->tv_sec;
    struct tm Timeinfo;

    localtime_r(&Now, &Timeinfo);

    ESP_LOGD(TAG, "SNTP time synchronized: %04d-%02d-%02d %02d:%02d:%02d",
             Timeinfo.tm_year + 1900, Timeinfo.tm_mon + 1, Timeinfo.tm_mday,
             Timeinfo.tm_hour, Timeinfo.tm_min, Timeinfo.tm_sec);

    _TimeManager_State.lastSNTP_Sync = Now;
    _TimeManager_State.activeSource = TIME_SOURCE_SNTP;
    _TimeManager_State.timeSynchronized = true;
    _TimeManager_State.sntpSyncCount++;

    /* Post time synchronized event */
    esp_event_post(TIME_EVENTS, TIME_EVENT_SYNCHRONIZED, &Timeinfo, sizeof(struct tm), portMAX_DELAY);

    /* Backup time to RTC if available */
    if (_TimeManager_State.hasRTC) {
        esp_err_t Error;

        Error = DevicesManager_SetTime(&Timeinfo);
        if (Error == ESP_OK) {
            ESP_LOGD(TAG, "Time backed up to RTC");

            _TimeManager_State.lastRTC_Backup = Now;
        } else {
            ESP_LOGW(TAG, "Failed to backup time to RTC: %d!", Error);
        }
    }
}

/** @brief          Periodic timer callback for time synchronization.
 *  @param p_Arg    Timer argument
 */
static void TimeManager_Sync_Timer_Callback(void *p_Arg)
{
    time_t Now;

    time(&Now);

    /* If network is available and SNTP sync is due */
    if (_TimeManager_State.hasNetwork) {
        time_t TimeSinceSync;

        TimeSinceSync = Now - _TimeManager_State.lastSNTP_Sync;

        if (TimeSinceSync >= TIME_MANAGER_SYNC_INTERVAL_SEC) {
            ESP_LOGD(TAG, "Periodic SNTP synchronization (last sync: %ld sec ago)", TimeSinceSync);

            /* Restart SNTP to trigger new sync */
            esp_sntp_restart();
        }

        /* Backup system time to RTC periodically */
        if (_TimeManager_State.hasRTC && _TimeManager_State.timeSynchronized) {
            time_t TimeSinceBackup = Now - _TimeManager_State.lastRTC_Backup;

            if (TimeSinceBackup >= TIME_MANAGER_RTC_BACKUP_INTERVAL_SEC) {
                struct tm Timeinfo;
                localtime_r(&Now, &Timeinfo);

                if (DevicesManager_SetTime(&Timeinfo) == ESP_OK) {
                    ESP_LOGD(TAG, "Periodic RTC backup");

                    _TimeManager_State.lastRTC_Backup = Now;
                }
            }
        }
    }
}

esp_err_t TimeManager_Init(void *p_RTC_Handle)
{
    esp_err_t Error;
    struct tm RtcTime;

    if (_TimeManager_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Time Manager");

    memset(&_TimeManager_State, 0, sizeof(TimeManager_State_t));

    _TimeManager_State.hasRTC = true;
    ESP_LOGD(TAG, "RTC available for time backup");

    /* Try to load time from RTC */
    Error = DevicesManager_GetTime(&RtcTime);
    if (Error == ESP_OK) {
        /* Validate RTC time (check if year is reasonable) */
        if ((RtcTime.tm_year + 1900) >= TIME_MANAGER_VALID_YEAR_MIN) {
            /* Set system time from RTC */
            time_t T = mktime(&RtcTime);
            struct timeval Tv = {.tv_sec = T, .tv_usec = 0};

            settimeofday(&Tv, NULL);

            _TimeManager_State.activeSource = TIME_SOURCE_RTC;
            _TimeManager_State.timeSynchronized = true;
            _TimeManager_State.lastRTC_Sync = T;
            _TimeManager_State.rtcSyncCount++;

            ESP_LOGD(TAG, "System time initialized from RTC: %04d-%02d-%02d %02d:%02d:%02d",
                     RtcTime.tm_year + 1900, RtcTime.tm_mon + 1, RtcTime.tm_mday,
                     RtcTime.tm_hour, RtcTime.tm_min, RtcTime.tm_sec);
        } else {
            ESP_LOGW(TAG, "RTC time invalid (year: %d), waiting for SNTP",
                     RtcTime.tm_year + 1900);
        }
    } else {
        ESP_LOGW(TAG, "Failed to read time from RTC: %d!", Error);
    }

    /* Set timezone to UTC by default */
    setenv("TZ", "UTC-0", 1);
    tzset();

    /* Create periodic sync timer */
    const esp_timer_create_args_t TimerArgs = {
        .callback = &TimeManager_Sync_Timer_Callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "time_sync",
        .skip_unhandled_events = false,
    };

    Error = esp_timer_create(&TimerArgs, &_TimeManager_State.syncTimer);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sync timer: %d!", Error);

        return Error;
    }

    /* Start timer with 60 second period */
    Error = esp_timer_start_periodic(_TimeManager_State.syncTimer, 60 * 1000000ULL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sync timer: %d!", Error);

        esp_timer_delete(_TimeManager_State.syncTimer);

        return Error;
    }

    _TimeManager_State.isInitialized = true;

    ESP_LOGD(TAG, "Time Manager initialized (Source: %s)",
             _TimeManager_State.activeSource == TIME_SOURCE_RTC ? "RTC" :
             _TimeManager_State.activeSource == TIME_SOURCE_SNTP ? "SNTP" : "None");

    return ESP_OK;
}

esp_err_t TimeManager_Deinit(void)
{
    if (_TimeManager_State.isInitialized == false) {
        return ESP_OK;
    }

    if (_TimeManager_State.syncTimer != NULL) {
        esp_timer_stop(_TimeManager_State.syncTimer);
        esp_timer_delete(_TimeManager_State.syncTimer);
        _TimeManager_State.syncTimer = NULL;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    _TimeManager_State.isInitialized = false;

    ESP_LOGD(TAG, "Time Manager deinitialized");

    return ESP_OK;
}

esp_err_t TimeManager_OnNetworkConnected(void)
{
    ESP_LOGD(TAG, "Network connected, starting SNTP synchronization");

    _TimeManager_State.hasNetwork = true;

    /* Initialize SNTP if not already done */
    if (esp_sntp_enabled()) {
        /* Restart SNTP to immediately try to sync time */
        esp_sntp_restart();
    } else {
        ESP_LOGD(TAG, "SNTP initialized");

        /* First time init */
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        sntp_set_time_sync_notification_cb(TimeManager_SNTP_Sync_Callback);
        esp_sntp_init();
    }

    return ESP_OK;
}

esp_err_t TimeManager_OnNetworkDisconnected(void)
{
    ESP_LOGD(TAG, "Network disconnected, switching to RTC time source");

    _TimeManager_State.hasNetwork = false;

    /* Switch to RTC if available */
    if (_TimeManager_State.hasRTC) {
        TimeManager_Source_t OldSource = _TimeManager_State.activeSource;
        _TimeManager_State.activeSource = TIME_SOURCE_RTC;

        /* Post source changed event if source actually changed */
        if (OldSource != TIME_SOURCE_RTC) {
            esp_event_post(TIME_EVENTS, TIME_EVENT_SOURCE_CHANGED,
                           &_TimeManager_State.activeSource,
                           sizeof(TimeManager_Source_t), portMAX_DELAY);
        }

        ESP_LOGD(TAG, "Now using RTC as time source");
    } else {
        ESP_LOGW(TAG, "No RTC available, system time will drift");
        _TimeManager_State.activeSource = TIME_SOURCE_SYSTEM;
    }

    return ESP_OK;
}

esp_err_t TimeManager_GetTime(struct tm *p_Time, TimeManager_Source_t *p_Source)
{
    time_t Now;

    if (p_Time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time(&Now);

    localtime_r(&Now, p_Time);

    if (p_Source != NULL) {
        *p_Source = _TimeManager_State.activeSource;
    }

    return ESP_OK;
}

esp_err_t TimeManager_GetTimestamp(time_t *p_Time, TimeManager_Source_t *p_Source)
{
    if (p_Time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time(p_Time);

    if (p_Source != NULL) {
        *p_Source = _TimeManager_State.activeSource;
    }

    return ESP_OK;
}

esp_err_t TimeManager_GetStatus(TimeManager_Status_t *p_Status)
{
    if (p_Status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    p_Status->ActiveSource = _TimeManager_State.activeSource;
    p_Status->SNTP_Available = _TimeManager_State.hasNetwork;
    p_Status->RTC_Available = _TimeManager_State.hasRTC;
    p_Status->LastSync_SNTP = _TimeManager_State.lastSNTP_Sync;
    p_Status->LastSync_RTC = _TimeManager_State.lastRTC_Sync;
    p_Status->SNTP_SyncCount = _TimeManager_State.sntpSyncCount;
    p_Status->RTC_SyncCount = _TimeManager_State.rtcSyncCount;

    return ESP_OK;
}

esp_err_t TimeManager_ForceSync(void)
{
    if (_TimeManager_State.hasNetwork == false) {
        ESP_LOGW(TAG, "Cannot force sync: no network connection");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Forcing SNTP synchronization");

    esp_sntp_restart();

    return ESP_OK;
}

bool TimeManager_IsTimeSynchronized(void)
{
    return _TimeManager_State.timeSynchronized;
}

esp_err_t TimeManager_GetTimeString(char *p_Buffer, size_t Size, const char *Format)
{
    esp_err_t Error;
    struct tm Timeinfo;
    size_t Written;

    if ((p_Buffer == NULL) || (Format == NULL) || (Size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = TimeManager_GetTime(&Timeinfo, NULL);
    if (Error != ESP_OK) {
        return Error;
    }

    Written = strftime(p_Buffer, Size, Format, &Timeinfo);
    if (Written == 0) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t TimeManager_SetTimezone(const char *p_Timezone)
{
    if (p_Timezone == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Setting timezone to: %s", p_Timezone);

    setenv("TZ", p_Timezone, 1);
    tzset();

    return ESP_OK;
}