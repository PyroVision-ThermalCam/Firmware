/*
 * http_handler_time.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for time-related endpoints.
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

#include <sys/time.h>
#include <cJSON.h>

#include "http_handler.h"
#include "../Manager/Settings/settingsManager.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Time-Handler";

esp_err_t HTTP_Handler_Time(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Epoch;
    cJSON *Timezone;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = HTTP_Server_ParseJSON(p_Request);
    if (JSON == NULL) {
        return HTTP_Server_SendError(p_Request, 400, "Invalid JSON");
    }

    Epoch = cJSON_GetObjectItem(JSON, "epoch");
    Timezone = cJSON_GetObjectItem(JSON, "timezone");

    if (cJSON_IsNumber(Epoch) == false) {
        cJSON_Delete(JSON);

        return HTTP_Server_SendError(p_Request, 400, "Missing epoch field");
    }

    /* Set system time */
    struct timeval tv = {
        .tv_sec = static_cast<time_t>(Epoch->valuedouble),
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    /* Set timezone if provided */
    if (cJSON_IsString(Timezone) && (Timezone->valuestring != NULL)) {
        SNTP_SetTimezone(Timezone->valuestring);
    }

    ESP_LOGD(TAG, "Time set to epoch: %f", Epoch->valuedouble);

    cJSON_Delete(JSON);

    /* Send response */
    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}