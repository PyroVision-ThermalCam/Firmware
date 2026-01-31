/*
 * http_server.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP server implementation.
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
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>

#include <sys/time.h>

#include <cJSON.h>
#include <cstring>

#include "http_server.h"
#include "ImageEncoder/imageEncoder.h"
#include "../networkTypes.h"
#include "../Provisioning/provisionHandlers.h"
#include "../SNTP/sntp.h"

#define HTTP_SERVER_API_BASE_PATH           "/api/v1"
#define HTTP_SERVER_API_KEY_HEADER          "X-API-Key"

typedef struct {
    bool isInitialized;
    bool isRunning;
    httpd_handle_t Handle;
    Network_HTTP_Server_Config_t Config;
    Network_Thermal_Frame_t *ThermalFrame;
    uint32_t RequestCount;
    uint32_t StartTime;
} HTTP_Server_State_t;

static HTTP_Server_State_t _HTTPServer_State;

static const char *TAG = "http_server";

/** @brief              Check API key authentication.
 *  @param p_Request    HTTP request handle
 *  @return             true if authenticated
 */
static bool HTTP_Server_CheckAuth(httpd_req_t *p_Request)
{
    char ApiKey[64] = {0};

    if (_HTTPServer_State.Config.API_Key == NULL) {
        return true;
    }

    if (httpd_req_get_hdr_value_str(p_Request, HTTP_SERVER_API_KEY_HEADER, ApiKey, sizeof(ApiKey)) != ESP_OK) {
        return false;
    }

    return (strcmp(ApiKey, _HTTPServer_State.Config.API_Key) == 0);
}

/** @brief              Send JSON response.
 *  @param p_Request    HTTP request handle
 *  @param p_JSON       JSON object to send
 *  @param StatusCode   HTTP status code
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Server_SendJSON(httpd_req_t *p_Request, cJSON *p_JSON, int StatusCode)
{
    esp_err_t Error;

    char *JsonStr = cJSON_PrintUnformatted(p_JSON);
    if (JsonStr == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encoding failed");
        return ESP_FAIL;
    }

    if (StatusCode != 200) {
        char status_str[16];
        snprintf(status_str, sizeof(status_str), "%d", StatusCode);
        httpd_resp_set_status(p_Request, status_str);
    }

    httpd_resp_set_type(p_Request, "application/json");

    if (_HTTPServer_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_sendstr(p_Request, JsonStr);
    cJSON_free(JsonStr);

    return Error;
}

/** @brief              Send error response.
 *  @param p_Request    HTTP request handle
 *  @param StatusCode   HTTP status code
 *  @param p_Message    Error message
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Server_SendError(httpd_req_t *p_Request, int StatusCode, const char *p_Message)
{
    esp_err_t Error;

    cJSON *Json = cJSON_CreateObject();
    if (Json == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(Json, "error", p_Message);
    cJSON_AddNumberToObject(Json, "code", StatusCode);

    Error = HTTP_Server_SendJSON(p_Request, Json, StatusCode);
    cJSON_Delete(Json);

    return Error;
}

/** @brief              Parse JSON from request body.
 *  @param p_Request    HTTP request handle
 *  @return             cJSON object or NULL on error
 */
static cJSON *HTTP_Server_ParseJSON(httpd_req_t *p_Request)
{
    int ContentLen = p_Request->content_len;
    if (ContentLen <= 0 || ContentLen > 4096) {
        return NULL;
    }

    char *Buffer = (char *)heap_caps_malloc(ContentLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (Buffer == NULL) {
        return NULL;
    }

    int Received = httpd_req_recv(p_Request, Buffer, ContentLen);
    if (Received != ContentLen) {
        free(Buffer);
        return NULL;
    }

    Buffer[ContentLen] = '\0';
    cJSON *Json = cJSON_Parse(Buffer);
    free(Buffer);

    return Json;
}

/** @brief              Handler for POST /api/v1/time.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Time(httpd_req_t *p_Request)
{
    esp_err_t Error;

    _HTTPServer_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    cJSON *json = HTTP_Server_ParseJSON(p_Request);
    if (json == NULL) {
        return HTTP_Server_SendError(p_Request, 400, "Invalid JSON");
    }

    cJSON *epoch = cJSON_GetObjectItem(json, "epoch");
    cJSON *timezone = cJSON_GetObjectItem(json, "timezone");

    if (cJSON_IsNumber(epoch) == false) {
        cJSON_Delete(json);
        return HTTP_Server_SendError(p_Request, 400, "Missing epoch field");
    }

    /* Set system time */
    struct timeval tv = {
        .tv_sec = (time_t)epoch->valuedouble,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    /* Set timezone if provided */
    if (cJSON_IsString(timezone) && (timezone->valuestring != NULL)) {
        SNTP_SetTimezone(timezone->valuestring);
    }

    ESP_LOGD(TAG, "Time set to epoch: %f", epoch->valuedouble);

    cJSON_Delete(json);

    /* Send response */
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    Error = HTTP_Server_SendJSON(p_Request, response, 200);
    cJSON_Delete(response);

    return Error;
}

/** @brief              Handler for GET /api/v1/image.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Image(httpd_req_t *p_Request)
{
    esp_err_t Error;
    Network_Encoded_Image_t encoded;
    char query[128] = {0};
    Network_ImageFormat_t format = NETWORK_IMAGE_FORMAT_JPEG;
    Server_Palette_t palette = PALETTE_IRON;

    _HTTPServer_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    } else if (_HTTPServer_State.ThermalFrame == NULL) {
        return HTTP_Server_SendError(p_Request, 503, "No thermal data available");
    }

    if (httpd_req_get_url_query_str(p_Request, query, sizeof(query)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(query, "format", param, sizeof(param)) == ESP_OK) {
            if (strcmp(param, "png") == 0) {
                format = NETWORK_IMAGE_FORMAT_PNG;
            } else if (strcmp(param, "raw") == 0) {
                format = NETWORK_IMAGE_FORMAT_RAW;
            }
        }
        if (httpd_query_key_value(query, "palette", param, sizeof(param)) == ESP_OK) {
            if (strcmp(param, "gray") == 0) {
                palette = PALETTE_GRAY;
            } else if (strcmp(param, "rainbow") == 0) {
                palette = PALETTE_RAINBOW;
            }
        }
    }

    if (xSemaphoreTake(_HTTPServer_State.ThermalFrame->mutex, 100 / portTICK_PERIOD_MS) != pdTRUE) {
        return HTTP_Server_SendError(p_Request, 503, "Frame busy");
    }

    Error = ImageEncoder_Encode(_HTTPServer_State.ThermalFrame, format, palette, &encoded);

    xSemaphoreGive(_HTTPServer_State.ThermalFrame->mutex);

    if (Error != ESP_OK) {
        return HTTP_Server_SendError(p_Request, 500, "Image encoding failed");
    }

    /* Set content type */
    switch (format) {
        case NETWORK_IMAGE_FORMAT_JPEG:
            httpd_resp_set_type(p_Request, "image/jpeg");
            break;
        case NETWORK_IMAGE_FORMAT_PNG:
            httpd_resp_set_type(p_Request, "image/png");
            break;
        case NETWORK_IMAGE_FORMAT_RAW:
            httpd_resp_set_type(p_Request, "application/octet-stream");
            break;
    }

    if (_HTTPServer_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    /* Send image data */
    Error = httpd_resp_send(p_Request, (const char *)encoded.data, encoded.size);

    ImageEncoder_Free(&encoded);

    return Error;
}

/** @brief              Handler for GET /api/v1/telemetry.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Telemetry(httpd_req_t *p_Request)
{
    esp_err_t Error;

    _HTTPServer_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    /* Build telemetry response */
    cJSON *json = cJSON_CreateObject();

    /* Uptime */
    uint32_t uptime = (esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(json, "uptime_s", uptime);

    /* Sensor temperature (from thermal frame if available) */
    if (_HTTPServer_State.ThermalFrame != NULL) {
        cJSON_AddNumberToObject(json, "sensor_temp_c", _HTTPServer_State.ThermalFrame->temp_avg);
    }

    /* Supply voltage (placeholder) */
    cJSON_AddNumberToObject(json, "supply_voltage_v", 0);

    /* WiFi RSSI */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(json, "wifi_rssi_dbm", ap_info.rssi);
    } else {
        cJSON_AddNumberToObject(json, "wifi_rssi_dbm", 0);
    }

    /* SD card info (placeholder) */
    cJSON *sdcard = cJSON_CreateObject();
    cJSON_AddBoolToObject(sdcard, "present", false);
    cJSON_AddNumberToObject(sdcard, "free_mb", 0);
    cJSON_AddItemToObject(json, "sdcard", sdcard);

    Error = HTTP_Server_SendJSON(p_Request, json, 200);
    cJSON_Delete(json);

    return Error;
}

/** @brief              Handler for POST /api/v1/update (OTA).
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Update(httpd_req_t *p_Request)
{
    int content_len;
    int received = 0;
    int total_received = 0;
    esp_err_t Error;
    esp_ota_handle_t ota_handle;

    _HTTPServer_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL) {
        return HTTP_Server_SendError(p_Request, 500, "No OTA partition found");
    }

    ESP_LOGI(TAG, "OTA update starting, partition: %s", update_partition->label);

    Error = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %d!", Error);
        return HTTP_Server_SendError(p_Request, 500, "OTA begin failed");
    }

    /* Receive firmware data */
    char *buffer = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        esp_ota_abort(ota_handle);

        return HTTP_Server_SendError(p_Request, 500, "Memory allocation failed");
    }

    content_len = p_Request->content_len;

    while (total_received < content_len) {
        received = httpd_req_recv(p_Request, buffer, 1024);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buffer);
            esp_ota_abort(ota_handle);

            return HTTP_Server_SendError(p_Request, 500, "Receive failed");
        }

        Error = esp_ota_write(ota_handle, buffer, received);
        if (Error != ESP_OK) {
            free(buffer);
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %d!", Error);

            return HTTP_Server_SendError(p_Request, 500, "OTA write failed");
        }

        total_received += received;
        ESP_LOGD(TAG, "OTA progress: %d/%d bytes", total_received, content_len);
    }

    free(buffer);

    Error = esp_ota_end(ota_handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %d", Error);
        return HTTP_Server_SendError(p_Request, 500, "OTA end failed");
    }

    Error = esp_ota_set_boot_partition(update_partition);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d!", Error);
        return HTTP_Server_SendError(p_Request, 500, "Set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting...");

    /* Send response */
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "updating");
    cJSON_AddStringToObject(response, "message", "Firmware upload successful. Device will reboot after update.");
    Error = HTTP_Server_SendJSON(p_Request, response, 200);
    cJSON_Delete(response);

    /* Reboot after response is sent */
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();

    return Error;
}

/** @brief              Handler for CORS preflight requests.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Options(httpd_req_t *p_Request)
{
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    httpd_resp_set_hdr(p_Request, "Access-Control-Max-Age", "86400");
    httpd_resp_send(p_Request, NULL, 0);

    return ESP_OK;
}

static const httpd_uri_t _URI_Time = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/time",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Time,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Image = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/image",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Image,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Telemetry = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/telemetry",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Telemetry,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Update = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/update",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Update,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Options = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/*",
    .method    = HTTP_OPTIONS,
    .handler   = HTTP_Handler_Options,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Root,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Scan = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/provision/scan",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Scan,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Connect = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/provision",
    .method    = HTTP_POST,
    .handler   = Provision_Handler_Connect,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_CaptivePortal_Generate204 = {
    .uri       = "/generate_204",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_CaptivePortal,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_CaptivePortal_Generate204_NoUnderscore = {
    .uri       = "/generate204",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_CaptivePortal,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

esp_err_t HTTP_Server_Init(const Network_HTTP_Server_Config_t *p_Config)
{
    if (p_Config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_HTTPServer_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized, updating config");
        memcpy(&_HTTPServer_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing HTTP server");

    memcpy(&_HTTPServer_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
    _HTTPServer_State.Handle = NULL;
    _HTTPServer_State.ThermalFrame = NULL;
    _HTTPServer_State.RequestCount = 0;
    _HTTPServer_State.isInitialized = true;

    return ESP_OK;
}

void HTTP_Server_Deinit(void)
{
    if (_HTTPServer_State.isInitialized == false) {
        return;
    }

    HTTP_Server_Stop();
    _HTTPServer_State.isInitialized = false;

    ESP_LOGD(TAG, "HTTP server deinitialized");
}

esp_err_t HTTP_Server_Start(void)
{
    esp_err_t Error;

    if (_HTTPServer_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_HTTPServer_State.isRunning) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _HTTPServer_State.Config.Port;
    config.max_uri_handlers = 12;

    /* Use only 2 sockets for provisioning */
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    /* Very short timeouts to allow quick shutdown */
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.keep_alive_enable = false;  /* Disable keep-alive to reduce memory */

    /* Increased stack and header size for Android captive portal requests */
    config.stack_size = 5120;
    config.max_resp_headers = 8;
    config.max_req_hdr_len = 1024;  /* Android sends large headers */

    /* Core affinity and priority - run on core 1 to avoid blocking IDLE0 */
    config.ctrl_port = 0;
    config.core_id = 1;
    config.task_priority = 3;

    ESP_LOGD(TAG, "Starting HTTP server on port %d", config.server_port);

    Error = httpd_start(&_HTTPServer_State.Handle, &config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d!", Error);
        return Error;
    }

    /* Register URI handlers */
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Provision_Root);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Provision_Scan);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Provision_Connect);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_CaptivePortal_Generate204);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_CaptivePortal_Generate204_NoUnderscore);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Time);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Image);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Telemetry);
    httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Update);

    if (_HTTPServer_State.Config.EnableCORS) {
        httpd_register_uri_handler(_HTTPServer_State.Handle, &_URI_Options);
    }

    _HTTPServer_State.isRunning = true;
    _HTTPServer_State.StartTime = esp_timer_get_time() / 1000000;

    ESP_LOGD(TAG, "HTTP server started");

    return ESP_OK;
}

esp_err_t HTTP_Server_Stop(void)
{
    if (_HTTPServer_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping HTTP server");

    _HTTPServer_State.isRunning = false;
    _HTTPServer_State.Handle = NULL;

    return ESP_OK;
}

bool HTTP_Server_isRunning(void)
{
    return _HTTPServer_State.isRunning;
}

void HTTP_Server_SetThermalFrame(Network_Thermal_Frame_t *p_Frame)
{
    _HTTPServer_State.ThermalFrame = p_Frame;
}

httpd_handle_t HTTP_Server_GetHandle(void)
{
    return _HTTPServer_State.Handle;
}
