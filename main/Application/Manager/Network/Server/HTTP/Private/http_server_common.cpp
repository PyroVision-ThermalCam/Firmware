/*
 * http_server_common.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for common endpoints.
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

#include <cJSON.h>
#include <esp_log.h>

#include "http_handler.h"

extern HTTP_Server_State_t _HTTP_Server_State;

bool HTTP_Server_CheckAuth(httpd_req_t *p_Request)
{
    char ApiKey[64] = {};

    if (_HTTP_Server_State.Config.API_Key[0] == '\0') {
        return true;
    } else if (httpd_req_get_hdr_value_str(p_Request, HTTP_SERVER_API_KEY_HEADER, ApiKey, sizeof(ApiKey)) != ESP_OK) {
        return false;
    }

    return (strncmp(ApiKey, _HTTP_Server_State.Config.API_Key, sizeof(ApiKey)) == 0);
}

esp_err_t HTTP_Server_SendJSON(httpd_req_t *p_Request, cJSON *p_JSON, int StatusCode)
{
    esp_err_t Error;
    char *JSON;

    JSON = cJSON_PrintUnformatted(p_JSON);
    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encoding failed");

        return ESP_FAIL;
    }

    if (StatusCode != 200) {
        char StatusStr[16];
        snprintf(StatusStr, sizeof(StatusStr), "%d", StatusCode);
        httpd_resp_set_status(p_Request, StatusStr);
    }

    httpd_resp_set_type(p_Request, "application/json");

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_sendstr(p_Request, JSON);
    cJSON_free(JSON);

    return Error;
}

esp_err_t HTTP_Server_SendError(httpd_req_t *p_Request, int StatusCode, const char *p_Message)
{
    esp_err_t Error;
    cJSON *JSON;

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");

        return ESP_FAIL;
    }

    cJSON_AddStringToObject(JSON, "error", p_Message);
    cJSON_AddNumberToObject(JSON, "code", StatusCode);

    Error = HTTP_Server_SendJSON(p_Request, JSON, StatusCode);
    cJSON_Delete(JSON);

    return Error;
}

cJSON *HTTP_Server_ParseJSON(httpd_req_t *p_Request)
{
    char *Buffer;
    cJSON *JSON;

    if ((p_Request->content_len <= 0) || (p_Request->content_len > 4096)) {
        return NULL;
    }

    Buffer = static_cast<char *>(heap_caps_malloc(p_Request->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (Buffer == NULL) {
        return NULL;
    }

    if (httpd_req_recv(p_Request, Buffer, p_Request->content_len) != p_Request->content_len) {
        heap_caps_free(Buffer);

        return NULL;
    }

    Buffer[p_Request->content_len] = '\0';
    JSON = cJSON_Parse(Buffer);
    heap_caps_free(Buffer);

    return JSON;
}