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
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>

#include <cJSON.h>
#include <cstring>
#include <strings.h>

#include "http_server.h"
#include "Private/http_handler.h"
#include "../../Provisioning/provisionHandlers.h"
#include "../../../Devices/devicesManager.h"
#include "../../../Network/networkManager.h"

HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Server";

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

static const httpd_uri_t _URI_Settings_POST = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/settings",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Settings,
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

static const httpd_uri_t _URI_Info = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/info",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Info,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Memory = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/memory",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Memory,
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

static const httpd_uri_t _URI_Provision_Favicon = {
    .uri       = "/favicon.ico",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Favicon,
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
    } else if (_HTTP_Server_State.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing HTTP server");

    __builtin_memcpy(&_HTTP_Server_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
    _HTTP_Server_State.Handle = NULL;
    _HTTP_Server_State.RawFrame = NULL;
    _HTTP_Server_State.RequestCount = 0;
    _HTTP_Server_State.IsInitialized = true;

    return ESP_OK;
}

void HTTP_Server_Deinit(void)
{
    if (_HTTP_Server_State.IsInitialized == false) {
        return;
    }

    HTTP_Server_Stop();
    _HTTP_Server_State.IsInitialized = false;

    ESP_LOGD(TAG, "HTTP server deinitialized");
}

esp_err_t HTTP_Server_Start(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_HTTP_Server_State.IsRunning) {
        ESP_LOGW(TAG, "Server already running");

        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _HTTP_Server_State.Config.Port;
    config.max_uri_handlers = 14;

    /* Use only 2 sockets for provisioning */
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    /* Very short timeouts to allow quick shutdown */
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.keep_alive_enable = false;  /* Disable keep-alive to reduce memory */

    /* Increased stack and header size for Android captive portal requests */
    config.stack_size = 6144;
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

    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Root);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Logo);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Scan);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Connect);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Favicon);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204_NoUnderscore);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Time);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Image);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Settings_POST);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Telemetry);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Info);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Memory);

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Options);
    }

    _HTTP_Server_State.IsRunning = true;
    _HTTP_Server_State.StartTime = esp_timer_get_time() / 1000000;

    ESP_LOGD(TAG, "HTTP server started");

    return ESP_OK;
}

esp_err_t HTTP_Server_Stop(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.IsRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping HTTP server");

    _HTTP_Server_State.IsRunning = false;

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
    return _HTTP_Server_State.IsRunning;
}

void HTTP_Server_SetRawFrame(ImageEncoder_Raw_t *p_Frame)
{
    _HTTP_Server_State.RawFrame = p_Frame;
}

httpd_handle_t HTTP_Server_GetHandle(void)
{
    return _HTTP_Server_State.Handle;
}

void HTTP_Server_SetLeptonTemperatures(float FPA, float Aux)
{
    _HTTP_Server_State.LeptonFPA = FPA;
    _HTTP_Server_State.LeptonAUX = Aux;
}

void HTTP_Server_SetDeviceTemperature(float Temperature)
{
    _HTTP_Server_State.DeviceTemperatureC = Temperature;
}