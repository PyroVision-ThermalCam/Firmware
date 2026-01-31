/*
 * sntp.cpp
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

#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_event.h>

#include "sntp.h"

ESP_EVENT_DEFINE_BASE(SNTP_EVENTS);

static const char *TAG = "sntp";

/** @brief      SNTP time synchronization callback.
 *  @param p_tv Pointer to timeval structure with synchronized time
 */
static void on_SNTP_Time_Sync(struct timeval *p_tv)
{
    ESP_LOGD(TAG, "Time synchronized via SNTP");

    esp_event_post(SNTP_EVENTS, SNTP_EVENT_SNTP_SYNCED, p_tv, sizeof(struct timeval), portMAX_DELAY);
}

esp_err_t SNTP_Init(const char* p_Timezone, const char* p_Server, uint32_t SyncInterval)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, p_Server);
    esp_sntp_set_time_sync_notification_cb(on_SNTP_Time_Sync);
    esp_sntp_init();

    SNTP_SetTimezone(p_Timezone);

    /* Setup automatic sync timer if interval is specified */
    esp_sntp_set_sync_interval(3600);

    return ESP_OK;
}

esp_err_t SNTP_Deinit(void)
{
    esp_sntp_stop();

    return ESP_OK;
}

esp_err_t SNTP_GetTime(uint8_t Retries)
{
    time_t Now;
    struct tm TimeInfo;
    uint8_t Retry = 0;

    ESP_LOGD(TAG, "Initializing SNTP");

    memset(&TimeInfo, 0, sizeof(struct tm));

    while ((TimeInfo.tm_year < (2016 - 1900)) && (++Retry < Retries)) {
        ESP_LOGD(TAG, "Waiting for system time... (%d/%d)", Retry, Retries);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&Now);
        localtime_r(&Now, &TimeInfo);
    }

    if (Retry == Retries) {
        ESP_LOGW(TAG, "Failed to synchronize time!");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Time synchronized successfully");

    time(&Now);
    localtime_r(&Now, &TimeInfo);

    return ESP_OK;
}

void SNTP_SetTimezone(const char* p_Timezone)
{
    setenv("TZ", p_Timezone, 1);
    tzset();

    esp_event_post(SNTP_EVENTS, SNTP_EVENT_TZ_CHANGED, p_Timezone, strlen(p_Timezone) + 1, portMAX_DELAY);
}