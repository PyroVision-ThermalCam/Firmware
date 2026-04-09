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

#include <string>
#include <cJSON.h>
#include <cstring>

#include "http_server.h"
#include "../ImageEncoder/imageEncoder.h"
#include "../../Provisioning/provisionHandlers.h"
#include "../../SNTP/sntp.h"

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

static HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Server";

/** @brief              Check API key authentication.
 *  @param p_Request    HTTP request handle
 *  @return             true if authenticated
 */
static bool HTTP_Server_CheckAuth(httpd_req_t *p_Request)
{
    std::string ApiKey(64, '\0');

    if (_HTTP_Server_State.Config.API_Key[0] == '\0') {
        return true;
    } else if (httpd_req_get_hdr_value_str(p_Request, HTTP_SERVER_API_KEY_HEADER, &ApiKey[0], ApiKey.size()) != ESP_OK) {
        return false;
    }

    ApiKey.resize(strlen(ApiKey.c_str()));

    return (ApiKey == _HTTP_Server_State.Config.API_Key);
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
    char *JSON;

    JSON = cJSON_PrintUnformatted(p_JSON);
    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encoding failed");

        return ESP_FAIL;
    }

    if (StatusCode != 200) {
        httpd_resp_set_status(p_Request, std::to_string(StatusCode).c_str());
    }

    httpd_resp_set_type(p_Request, "application/json");

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_sendstr(p_Request, JSON);
    cJSON_free(JSON);

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

/** @brief              Parse JSON from request body.
 *  @param p_Request    HTTP request handle
 *  @return             cJSON object or NULL on error
 */
static cJSON *HTTP_Server_ParseJSON(httpd_req_t *p_Request)
{
    char *Buffer;
    cJSON *JSON;
    uint32_t Caps;

    if ((p_Request->content_len <= 0) || (p_Request->content_len > 4096)) {
        return NULL;
    }

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    Caps = MALLOC_CAP_8BIT;
#endif

    Buffer = static_cast<char *>(heap_caps_malloc(p_Request->content_len + 1, Caps));
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

/** @brief              Handler for POST /api/v1/time.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Time(httpd_req_t *p_Request)
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

/** @brief              Handler for GET /api/v1/image.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Image(httpd_req_t *p_Request)
{
    esp_err_t Error;
    Network_Encoded_Image_t Encoded;
    std::string Query(128, '\0');
    Settings_Image_Format_t Format = IMAGE_FORMAT_JPEG;
    Server_Palette_t Palette = PALETTE_IRON;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    } else if (_HTTP_Server_State.ThermalFrame == NULL) {
        return HTTP_Server_SendError(p_Request, 503, "No thermal data available");
    }

    if (httpd_req_get_url_query_str(p_Request, &Query[0], Query.size()) == ESP_OK) {
        std::string Param(32, '\0');

        Query.resize(strlen(Query.c_str()));
        if (httpd_query_key_value(Query.c_str(), "format", &Param[0], Param.size()) == ESP_OK) {
            Param.resize(strlen(Param.c_str()));

            if (Param == "png") {
                Format = IMAGE_FORMAT_PNG;
            } else if (Param == "raw") {
                Format = IMAGE_FORMAT_RAW;
            }
        }

        if (httpd_query_key_value(Query.c_str(), "palette", &Param[0], Param.size()) == ESP_OK) {
            Param.resize(strlen(Param.c_str()));

            if (Param == "gray") {
                Palette = PALETTE_GRAY;
            } else if (Param == "rainbow") {
                Palette = PALETTE_RAINBOW;
            }
        }
    }

    if (xSemaphoreTake(_HTTP_Server_State.ThermalFrame->Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return HTTP_Server_SendError(p_Request, 503, "Frame busy");
    }

    Error = ImageEncoder_Encode(_HTTP_Server_State.ThermalFrame, Format, Palette, &Encoded);

    xSemaphoreGive(_HTTP_Server_State.ThermalFrame->Mutex);

    if (Error != ESP_OK) {
        return HTTP_Server_SendError(p_Request, 500, "Image encoding failed");
    }

    switch (Format) {
        case IMAGE_FORMAT_JPEG: {
            httpd_resp_set_type(p_Request, "image/jpeg");

            break;
        }
        case IMAGE_FORMAT_PNG: {
            httpd_resp_set_type(p_Request, "image/png");

            break;
        }
        case IMAGE_FORMAT_RAW: {
            httpd_resp_set_type(p_Request, "application/octet-stream");

            break;
        }
        case IMAGE_FORMAT_BITMAP: {
            httpd_resp_set_type(p_Request, "image/bmp");

            break;
        }
    }

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_send(p_Request, reinterpret_cast<const char *>(Encoded.Data), Encoded.Size);

    ImageEncoder_Free(&Encoded);

    return Error;
}

/** @brief              Handler for GET /api/v1/telemetry.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Telemetry(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Card;
    wifi_ap_record_t Info;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = cJSON_CreateObject();

    cJSON_AddNumberToObject(JSON, "uptime_s", esp_timer_get_time() / 1000000);

    /* Sensor temperature (from thermal frame if available) */
    if (_HTTP_Server_State.ThermalFrame != NULL) {
        // TODO
        //cJSON_AddNumberToObject(JSON, "sensor_temp_c", _HTTP_Server_State.ThermalFrame->temp_avg);
    }

    cJSON_AddNumberToObject(JSON, "supply_voltage_v", 0);

    if (esp_wifi_sta_get_ap_info(&Info) == ESP_OK) {
        cJSON_AddNumberToObject(JSON, "wifi_rssi_dbm", Info.rssi);
    } else {
        cJSON_AddNumberToObject(JSON, "wifi_rssi_dbm", 0);
    }

    Card = cJSON_CreateObject();
    cJSON_AddBoolToObject(Card, "present", false);
    cJSON_AddNumberToObject(Card, "free_mb", 0);
    cJSON_AddItemToObject(JSON, "sdcard", Card);

    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for POST /api/v1/update (OTA).
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Update(httpd_req_t *p_Request)
{
    uint32_t Caps;
    int Received;
    int Total_Received;
    esp_err_t Error;
    esp_ota_handle_t ota_handle;
    char *Buffer;
    cJSON *JSON;

    _HTTP_Server_State.RequestCount++;

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
        ESP_LOGE(TAG, "esp_ota_begin failed: 0x%X!", Error);

        return HTTP_Server_SendError(p_Request, 500, "OTA begin failed");
    }

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    Caps = MALLOC_CAP_8BIT;
#endif

    /* Receive firmware data */
    Buffer = static_cast<char *>(heap_caps_malloc(1024, Caps));
    if (Buffer == NULL) {
        esp_ota_abort(ota_handle);

        return HTTP_Server_SendError(p_Request, 500, "Memory allocation failed");
    }

    Received = 0;
    Total_Received = 0;
    while (Total_Received < p_Request->content_len) {
        Received = httpd_req_recv(p_Request, Buffer, 1024);
        if (Received <= 0) {
            if (Received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }

            heap_caps_free(Buffer);
            esp_ota_abort(ota_handle);

            return HTTP_Server_SendError(p_Request, 500, "Receive failed");
        }

        Error = esp_ota_write(ota_handle, Buffer, Received);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: 0x%X!", Error);

            heap_caps_free(Buffer);
            esp_ota_abort(ota_handle);

            return HTTP_Server_SendError(p_Request, 500, "OTA write failed");
        }

        Total_Received += Received;
        ESP_LOGD(TAG, "OTA progress: %d/%d bytes", Total_Received, p_Request->content_len);
    }

    heap_caps_free(Buffer);

    Error = esp_ota_end(ota_handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: 0x%X", Error);

        return HTTP_Server_SendError(p_Request, 500, "OTA end failed");
    }

    Error = esp_ota_set_boot_partition(update_partition);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: 0x%X!", Error);

        return HTTP_Server_SendError(p_Request, 500, "Set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting...");

    /* Send response */
    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "updating");
    cJSON_AddStringToObject(JSON, "message", "Firmware upload successful. Device will reboot after update.");
    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    /* Reboot after response is sent */
    vTaskDelay(pdMS_TO_TICKS(1000));
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

static const httpd_uri_t _URI_Provision_Logo = {
    .uri       = "/logo.png",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Logo,
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
    } else if (_HTTP_Server_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized, updating config");

        memcpy(&_HTTP_Server_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing HTTP server");

    memcpy(&_HTTP_Server_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
    _HTTP_Server_State.Handle = NULL;
    _HTTP_Server_State.ThermalFrame = NULL;
    _HTTP_Server_State.RequestCount = 0;
    _HTTP_Server_State.isInitialized = true;

    return ESP_OK;
}

void HTTP_Server_Deinit(void)
{
    if (_HTTP_Server_State.isInitialized == false) {
        return;
    }

    HTTP_Server_Stop();
    _HTTP_Server_State.isInitialized = false;

    ESP_LOGD(TAG, "HTTP server deinitialized");
}

esp_err_t HTTP_Server_Start(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_HTTP_Server_State.isRunning) {
        ESP_LOGW(TAG, "Server already running");

        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _HTTP_Server_State.Config.Port;
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

    Error = httpd_start(&_HTTP_Server_State.Handle, &config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: 0x%X!", Error);

        return Error;
    }

    /* Register URI handlers */
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Root);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Logo);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Scan);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Connect);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204_NoUnderscore);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Time);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Image);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Telemetry);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Update);

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Options);
    }

    _HTTP_Server_State.isRunning = true;
    _HTTP_Server_State.StartTime = esp_timer_get_time() / 1000000;

    ESP_LOGD(TAG, "HTTP server started");

    return ESP_OK;
}

esp_err_t HTTP_Server_Stop(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping HTTP server");

    _HTTP_Server_State.isRunning = false;

    if (_HTTP_Server_State.Handle != NULL) {
        Error = httpd_stop(_HTTP_Server_State.Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop HTTP server: 0x%X", Error);
        }
        _HTTP_Server_State.Handle = NULL;
    }

    return ESP_OK;
}

bool HTTP_Server_IsRunning(void)
{
    return _HTTP_Server_State.isRunning;
}

void HTTP_Server_SetThermalFrame(Network_Thermal_Frame_t *p_Frame)
{
    _HTTP_Server_State.ThermalFrame = p_Frame;
}

httpd_handle_t HTTP_Server_GetHandle(void)
{
    return _HTTP_Server_State.Handle;
}
