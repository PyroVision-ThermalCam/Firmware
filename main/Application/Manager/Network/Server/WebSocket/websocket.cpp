/*
 * websocket_handler.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WebSocket handler implementation.
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
#include <cJSON.h>

#include <cstring>

#include "websocket.h"
#include "../ImageEncoder/imageEncoder.h"

/** @brief WebSocket client state.
 */
typedef struct {
    int fd;
    bool active;
    bool stream_enabled;
    bool telemetry_enabled;
    Network_ImageFormat_t stream_format;
    uint8_t stream_fps;
    uint32_t telemetry_interval_ms;
    uint32_t last_telemetry_time;
    uint32_t last_frame_time;
} WS_Client_t;

typedef struct {
    bool isInitialized;
    bool isRunning;
    httpd_handle_t ServerHandle;
    Network_HTTP_Server_Config_t Config;
    WS_Client_t Clients[CONFIG_NETWORK_WEBSOCKET_CLIENTS];
    uint8_t ClientCount;
    Network_Thermal_Frame_t *ThermalFrame;
    SemaphoreHandle_t ClientsMutex;
    TaskHandle_t BroadcastTask;
    QueueHandle_t FrameReadyQueue;
} WebSocket_State_t;

static WebSocket_State_t _WSHandler_State;

static const char *TAG = "websocket_handler";

/** @brief      Find client by file descriptor.
 *  @param FD   File descriptor
 *  @return     Pointer to client or NULL if not found
 */
static WS_Client_t *WS_FindClient(int FD)
{
    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (_WSHandler_State.Clients[i].active && _WSHandler_State.Clients[i].fd == FD) {
            return &_WSHandler_State.Clients[i];
        }
    }

    xSemaphoreGive(_WSHandler_State.ClientsMutex);

    return NULL;
}

/** @brief      Add a new client.
 *  @param FD   File descriptor
 *  @return     Pointer to new client or NULL if full
 */
static WS_Client_t *WS_AddClient(int FD)
{
    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (_WSHandler_State.Clients[i].active == false) {
            _WSHandler_State.Clients[i].fd = FD;
            _WSHandler_State.Clients[i].active = true;
            _WSHandler_State.Clients[i].stream_enabled = false;
            _WSHandler_State.Clients[i].telemetry_enabled = false;
            _WSHandler_State.Clients[i].stream_format = NETWORK_IMAGE_FORMAT_JPEG;
            _WSHandler_State.Clients[i].stream_fps = 8;
            _WSHandler_State.Clients[i].telemetry_interval_ms = 1000;
            _WSHandler_State.Clients[i].last_telemetry_time = 0;
            _WSHandler_State.Clients[i].last_frame_time = 0;
            _WSHandler_State.ClientCount++;

            xSemaphoreGive(_WSHandler_State.ClientsMutex);

            ESP_LOGI(TAG, "Client added: fd=%d, total=%d", FD, _WSHandler_State.ClientCount);
            return &_WSHandler_State.Clients[i];
        }
    }

    xSemaphoreGive(_WSHandler_State.ClientsMutex);

    ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", FD);

    return NULL;
}

/** @brief      Remove a client.
 *  @param FD   File descriptor
 */
static void WS_RemoveClient(int FD)
{
    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (_WSHandler_State.Clients[i].active && _WSHandler_State.Clients[i].fd == FD) {
            _WSHandler_State.Clients[i].active = false;
            _WSHandler_State.ClientCount--;

            ESP_LOGI(TAG, "Client removed: fd=%d, total=%d", FD, _WSHandler_State.ClientCount);
            break;
        }
    }

    xSemaphoreGive(_WSHandler_State.ClientsMutex);
}

/** @brief          Send JSON message to a client.
 *  @param FD       File descriptor
 *  @param p_Cmd    Command/Event name
 *  @param p_Data   Data JSON object (will not be freed, can be NULL)
 *  @return         ESP_OK on success
 */
static esp_err_t WS_SendJSON(int FD, const char *p_Cmd, cJSON *p_Data)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "cmd", p_Cmd);

    if (p_Data != NULL) {
        cJSON_AddItemReferenceToObject(msg, "data", p_Data);
    }

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_str,
        .len = strlen(json_str),
    };

    esp_err_t err = httpd_ws_send_frame_async(_WSHandler_State.ServerHandle, FD, &Frame);

    cJSON_free(json_str);

    return err;
}

/** @brief          Send binary frame to a client.
 *  @param FD       File descriptor
 *  @param p_Data   Binary data
 *  @param Length   Data length
 *  @return         ESP_OK on success
 */
static esp_err_t WS_SendBinary(int FD, const uint8_t *p_Data, size_t Length)
{
    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = (uint8_t *)p_Data,
        .len = Length,
    };

    /* Try to send with retry logic to handle queue congestion */
    esp_err_t err = ESP_FAIL;
    for (uint8_t retry = 0; retry < 3; retry++) {
        err = httpd_ws_send_frame_async(_WSHandler_State.ServerHandle, FD, &Frame);

        if (err == ESP_OK) {
            /* Send queued successfully - add small delay to prevent queue overflow */
            vTaskDelay(5 / portTICK_PERIOD_MS);
            break;
        }

        /* Queue might be full, wait and retry */
        ESP_LOGW(TAG, "Failed to queue frame to fd=%d (retry %d): %s",
                 FD, retry + 1, esp_err_to_name(err));
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send binary frame to fd=%d after retries: %s (len=%zu)",
                 FD, esp_err_to_name(err), Length);
    }

    return err;
}

/** @brief              Handle start command.
 *  @param p_Client     Client pointer
 *  @param p_Data       Command data
 */
static void WS_HandleStart(WS_Client_t *p_Client, cJSON *p_Data)
{
    cJSON *fps = cJSON_GetObjectItem(p_Data, "fps");

    /* Set FPS (default 8) */
    if (cJSON_IsNumber(fps)) {
        p_Client->stream_fps = (uint8_t)fps->valueint;
        if (p_Client->stream_fps < 1) {
            p_Client->stream_fps = 1;
        }
        if (p_Client->stream_fps > 30) {
            p_Client->stream_fps = 30;
        }
    }

    /* Always use JPEG format for simplicity and efficiency */
    p_Client->stream_format = NETWORK_IMAGE_FORMAT_JPEG;
    p_Client->stream_enabled = true;
    p_Client->last_frame_time = 0;

    ESP_LOGI(TAG, "Stream started for fd=%d, fps=%d", p_Client->fd, p_Client->stream_fps);

    /* Send ACK */
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "fps", p_Client->stream_fps);
    WS_SendJSON(p_Client->fd, "started", response);
    cJSON_Delete(response);
}

/** @brief              Handle stop command.
 *  @param p_Client     Client pointer
 */
static void WS_HandleStop(WS_Client_t *p_Client)
{
    p_Client->stream_enabled = false;
    p_Client->last_frame_time = 0;

    ESP_LOGI(TAG, "Stream stopped for fd=%d", p_Client->fd);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    WS_SendJSON(p_Client->fd, "stopped", response);
    cJSON_Delete(response);
}

/** @brief              Handle subscribe command.
 *  @param p_Client     Client pointer
 *  @param p_Data       Command data
 */
static void WS_HandleTelemetrySubscribe(WS_Client_t *p_Client, cJSON *p_Data)
{
    cJSON *interval = cJSON_GetObjectItem(p_Data, "interval");

    if (cJSON_IsNumber(interval)) {
        p_Client->telemetry_interval_ms = (uint32_t)interval->valueint;
        if (p_Client->telemetry_interval_ms < 100) {
            p_Client->telemetry_interval_ms = 100;
        }
    }

    p_Client->telemetry_enabled = true;

    ESP_LOGI(TAG, "Telemetry subscribed for fd=%d, interval=%lu ms",
             p_Client->fd, p_Client->telemetry_interval_ms);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    WS_SendJSON(p_Client->fd, "subscribed", response);
    cJSON_Delete(response);
}

/** @brief              Handle unsubscribe command.
 *  @param p_Client     Client pointer
 */
static void WS_HandleTelemetryUnsubscribe(WS_Client_t *p_Client)
{
    p_Client->telemetry_enabled = false;

    ESP_LOGI(TAG, "Telemetry unsubscribed for fd=%d", p_Client->fd);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    WS_SendJSON(p_Client->fd, "unsubscribed", response);
    cJSON_Delete(response);
}

/** @brief              Process incoming WebSocket message.
 *  @param p_Client     Client pointer
 *  @param p_Data       Message data
 *  @param length       Message length
 */
static void WS_ProcessMessage(WS_Client_t *p_Client, const char *p_Data, size_t length)
{
    cJSON *json = cJSON_ParseWithLength(p_Data, length);
    if (json == NULL) {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", p_Client->fd);
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    cJSON *data = cJSON_GetObjectItem(json, "data");

    if (cJSON_IsString(cmd) == false) {
        ESP_LOGW(TAG, "Invalid message format from fd=%d", p_Client->fd);
        cJSON_Delete(json);
        return;
    }

    /* Handle commands */
    const char *cmd_str = cmd->valuestring;
    if (strcmp(cmd_str, "start") == 0) {
        WS_HandleStart(p_Client, data);
    } else if (strcmp(cmd_str, "stop") == 0) {
        WS_HandleStop(p_Client);
    } else if (strcmp(cmd_str, "subscribe") == 0) {
        WS_HandleTelemetrySubscribe(p_Client, data);
    } else if (strcmp(cmd_str, "unsubscribe") == 0) {
        WS_HandleTelemetryUnsubscribe(p_Client);
    } else {
        ESP_LOGW(TAG, "Unknown command from fd=%d: %s", p_Client->fd, cmd_str);
    }

    cJSON_Delete(json);
}

/** @brief WebSocket handler callback.
 */
static esp_err_t WS_Handler(httpd_req_t *p_Request)
{
    httpd_ws_frame_t Frame;
    esp_err_t Error;
    WS_Client_t *Client;
    int FD;

    /* Handle new connection */
    if (p_Request->method == HTTP_GET) {
        WS_Client_t *client = WS_AddClient(httpd_req_to_sockfd(p_Request));
        if (client == NULL) {
            httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "Max clients reached");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "WebSocket handshake with fd=%d", client->fd);
        return ESP_OK;
    }

    /* Handle WebSocket frame */
    memset(&Frame, 0, sizeof(httpd_ws_frame_t));

    /* Get frame info */
    Error = httpd_ws_recv_frame(p_Request, &Frame, 0);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame info: %d!", Error);
        return Error;
    }

    if (Frame.len > 0) {
        Frame.payload = (uint8_t *)heap_caps_malloc(Frame.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (Frame.payload == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer!");
            return ESP_ERR_NO_MEM;
        }

        Error = httpd_ws_recv_frame(p_Request, &Frame, Frame.len);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive frame: %d!", Error);
            free(Frame.payload);
            return Error;
        }

        Frame.payload[Frame.len] = '\0';
    }

    FD = httpd_req_to_sockfd(p_Request);
    Client = WS_FindClient(FD);

    switch (Frame.type) {
        case HTTPD_WS_TYPE_TEXT: {
            if (Client != NULL && Frame.payload != NULL) {
                WS_ProcessMessage(Client, (const char *)Frame.payload, Frame.len);
            }

            break;
        }
        case HTTPD_WS_TYPE_CLOSE: {
            ESP_LOGI(TAG, "WebSocket close from fd=%d", FD);
            WS_RemoveClient(FD);

            break;
        }
        case HTTPD_WS_TYPE_PING: {
            /* Must manually send PONG when handle_ws_control_frames=true */
            ESP_LOGD(TAG, "Ping received from fd=%d, sending Pong", FD);

            httpd_ws_frame_t pong_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_PONG,
                .payload = Frame.payload,  /* Echo back the ping payload */
                .len = Frame.len,
            };

            Error = httpd_ws_send_frame_async(_WSHandler_State.ServerHandle, FD, &pong_frame);
            if (Error != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send Pong to fd=%d: %d, retrying...", FD, Error);

                /* Pong fails sometimes. So we simply try again */
                vTaskDelay(10 / portTICK_PERIOD_MS);
                Error = httpd_ws_send_frame_async(_WSHandler_State.ServerHandle, FD, &pong_frame);
                if (Error != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send Pong to fd=%d again: %d, removing client!", FD, Error);
                    WS_RemoveClient(FD);
                }
            }

            break;
        }
        case HTTPD_WS_TYPE_PONG: {
            ESP_LOGD(TAG, "Pong received from fd=%d", FD);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unknown frame type %d from fd=%d", Frame.type, FD);

            break;
        }
    }

    if (Frame.payload != NULL) {
        free(Frame.payload);
    }

    return ESP_OK;
}

static const httpd_uri_t _URI_WebSocket = {
    .uri       = "/ws",
    .method    = HTTP_GET,
    .handler   = WS_Handler,
    .user_ctx  = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true,
    .supported_subprotocol = NULL,
};

esp_err_t WebSocket_Init(const Network_HTTP_Server_Config_t *p_Config)
{
    if (p_Config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_WSHandler_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WebSocket handler");

    memcpy(&_WSHandler_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
    memset(_WSHandler_State.Clients, 0, sizeof(_WSHandler_State.Clients));
    _WSHandler_State.ClientCount = 0;
    _WSHandler_State.ThermalFrame = NULL;
    _WSHandler_State.ServerHandle = NULL;
    _WSHandler_State.BroadcastTask = NULL;
    _WSHandler_State.FrameReadyQueue = NULL;
    _WSHandler_State.isRunning = false;

    _WSHandler_State.ClientsMutex = xSemaphoreCreateMutex();
    if (_WSHandler_State.ClientsMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create queue for frame ready notifications (queue size = 1, only latest matters) */
    _WSHandler_State.FrameReadyQueue = xQueueCreate(1, sizeof(uint8_t));
    if (_WSHandler_State.FrameReadyQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue!");
        vSemaphoreDelete(_WSHandler_State.ClientsMutex);
        return ESP_ERR_NO_MEM;
    }

    _WSHandler_State.isInitialized = true;

    return ESP_OK;
}

void WebSocket_Deinit(void)
{
    if (_WSHandler_State.isInitialized == false) {
        return;
    }

    WebSocket_StopTask();

    if (_WSHandler_State.FrameReadyQueue != NULL) {
        vQueueDelete(_WSHandler_State.FrameReadyQueue);
        _WSHandler_State.FrameReadyQueue = NULL;
    }

    if (_WSHandler_State.ClientsMutex != NULL) {
        vSemaphoreDelete(_WSHandler_State.ClientsMutex);
        _WSHandler_State.ClientsMutex = NULL;
    }

    _WSHandler_State.isInitialized = false;

    ESP_LOGI(TAG, "WebSocket handler deinitialized");
}

esp_err_t WebSocket_Register(httpd_handle_t p_ServerHandle)
{
    esp_err_t Error;

    if (_WSHandler_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_ServerHandle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    _WSHandler_State.ServerHandle = p_ServerHandle;

    Error = httpd_register_uri_handler(p_ServerHandle, &_URI_WebSocket);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket URI: %d!", Error);
        return Error;
    }

    ESP_LOGI(TAG, "WebSocket handler registered at /ws");

    return ESP_OK;
}

uint8_t WebSocket_GetClientCount(void)
{
    return _WSHandler_State.ClientCount;
}

bool WebSocket_HasClients(void)
{
    return _WSHandler_State.ClientCount > 0;
}

void WebSocket_SetThermalFrame(Network_Thermal_Frame_t *p_Frame)
{
    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);
    _WSHandler_State.ThermalFrame = p_Frame;
    xSemaphoreGive(_WSHandler_State.ClientsMutex);
}

/** @brief          Broadcast task function - runs in separate task to avoid blocking GUI.
 *  @param p_Param  Task parameter (unused)
 */
static void WS_BroadcastTask(void *p_Param)
{
    uint8_t Signal;
    Network_Encoded_Image_t Encoded;

    ESP_LOGI(TAG, "WebSocket broadcast task started");

    while (_WSHandler_State.isRunning) {
        /* Wait for frame ready notification (blocking, 100ms timeout) */
        if (xQueueReceive(_WSHandler_State.FrameReadyQueue, &Signal, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            uint32_t Now = esp_timer_get_time() / 1000;

            if ((_WSHandler_State.isInitialized == false) || (_WSHandler_State.ThermalFrame == NULL) ||
                (_WSHandler_State.ClientCount == 0)) {
                continue;
            }

            /* Encode frame ONCE for all clients (assume JPEG format for simplicity) */
            if (xSemaphoreTake(_WSHandler_State.ThermalFrame->Mutex, 50 / portTICK_PERIOD_MS) == pdTRUE) {
                esp_err_t err = ImageEncoder_Encode(_WSHandler_State.ThermalFrame,
                                                    NETWORK_IMAGE_FORMAT_JPEG, PALETTE_IRON, &Encoded);
                xSemaphoreGive(_WSHandler_State.ThermalFrame->Mutex);

                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to encode frame: %d!", err);
                    continue;
                }
            } else {
                /* Frame mutex busy, skip this frame */
                continue;
            }

            /* Send to all active streaming clients */
            xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);
            for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
                WS_Client_t *client = &_WSHandler_State.Clients[i];

                if ((client->active == false) || (client->stream_enabled == false)) {
                    continue;
                }

                /* Check frame rate limit */
                if ((Now - client->last_frame_time) < (1000 / client->stream_fps)) {
                    continue;
                }

                /* Copy FD before releasing mutex */
                int client_fd = client->fd;
                uint8_t client_idx = i;

                xSemaphoreGive(_WSHandler_State.ClientsMutex);
                esp_err_t send_err = WS_SendBinary(client_fd, Encoded.Data, Encoded.Size);
                xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

                /* Re-validate client is still active and same FD */
                if (_WSHandler_State.Clients[client_idx].active &&
                    _WSHandler_State.Clients[client_idx].fd == client_fd) {
                    if (send_err == ESP_OK) {
                        _WSHandler_State.Clients[client_idx].last_frame_time = Now;
                    } else {
                        ESP_LOGW(TAG, "Removing client fd=%d due to send failure", client_fd);
                        _WSHandler_State.Clients[client_idx].active = false;
                        _WSHandler_State.ClientCount--;
                    }
                }
            }

            xSemaphoreGive(_WSHandler_State.ClientsMutex);

            /* Free encoded frame after sending to all clients */
            ImageEncoder_Free(&Encoded);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    _WSHandler_State.BroadcastTask = NULL;
    vTaskDelete(NULL);
}

esp_err_t WebSocket_NotifyFrameReady(void)
{
    uint8_t Signal;

    if ((_WSHandler_State.isInitialized == false) || (_WSHandler_State.FrameReadyQueue == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (_WSHandler_State.ClientCount == 0) {
        return ESP_OK;
    }

    /* Signal frame ready (overwrite if queue full - only latest frame matters) */
    Signal = 1;
    xQueueOverwrite(_WSHandler_State.FrameReadyQueue, &Signal);

    return ESP_OK;
}

esp_err_t WebSocket_BroadcastTelemetry(void)
{
    uint32_t Now;

    if (_WSHandler_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (_WSHandler_State.ClientCount == 0) {
        return ESP_OK;
    }

    Now = esp_timer_get_time() / 1000;

    /* Build telemetry data */
    cJSON *data = cJSON_CreateObject();

    if (_WSHandler_State.ThermalFrame != NULL) {
        // TODO
        //cJSON_AddNumberToObject(data, "temp", _WSHandler_State.ThermalFrame->temp_avg);
    }

    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        WS_Client_t *client = &_WSHandler_State.Clients[i];

        if ((client->active == false) || (client->telemetry_enabled == false)) {
            continue;
        }

        /* Check interval */
        if ((Now - client->last_telemetry_time) < client->telemetry_interval_ms) {
            continue;
        }

        WS_SendJSON(client->fd, "telemetry", data);
        client->last_telemetry_time = Now;
    }

    xSemaphoreGive(_WSHandler_State.ClientsMutex);

    cJSON_Delete(data);

    return ESP_OK;
}

esp_err_t WebSocket_PingAll(void)
{
    if ((_WSHandler_State.isInitialized == false) || (_WSHandler_State.ServerHandle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_PING,
        .payload = NULL,
        .len = 0,
    };

    xSemaphoreTake(_WSHandler_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (_WSHandler_State.Clients[i].active) {
            httpd_ws_send_frame_async(_WSHandler_State.ServerHandle, _WSHandler_State.Clients[i].fd, &Frame);
        }
    }

    xSemaphoreGive(_WSHandler_State.ClientsMutex);

    return ESP_OK;
}

esp_err_t WebSocket_StartTask(void)
{
    if (_WSHandler_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_WSHandler_State.BroadcastTask != NULL) {
        ESP_LOGW(TAG, "Broadcast task already running");
        return ESP_OK;
    }

    _WSHandler_State.isRunning = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
                         WS_BroadcastTask,
                         "WS_Broadcast",
                         4096,
                         NULL,
                         5,
                         &_WSHandler_State.BroadcastTask,
                         1
                     );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create broadcast task");
        _WSHandler_State.isRunning = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void WebSocket_StopTask(uint32_t Timeout_ms)
{
    if (_WSHandler_State.BroadcastTask == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping WebSocket broadcast task...");

    _WSHandler_State.isRunning = false;
}
