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

#include <string>
#include <cstring>

#include "websocket.h"
#include "../ImageEncoder/imageEncoder.h"
#include "Private/websocketRemoteHandlers.h"

/** @brief WebSocket client state.
 */
typedef struct {
    int Fd;
    bool Active;
    bool StreamEnabled;
    bool TelemetryEnabled;
    Settings_Image_Format_t StreamFormat;
    uint8_t StreamFps;
    uint32_t TelemetryIntervalMs;
    uint32_t LastTelemetryTime;
    uint32_t LastFrameTime;
} WS_Client_t;

typedef struct {
    bool IsInitialized;
    bool IsRunning;
    httpd_handle_t ServerHandle;
    WS_Client_t Clients[CONFIG_NETWORK_WEBSOCKET_CLIENTS];
    uint8_t ClientCount;
    Network_Thermal_Frame_t *ThermalFrame;
    SemaphoreHandle_t ClientsMutex;
    TaskHandle_t BroadcastTask;
    QueueHandle_t FrameReadyQueue;
} WebSocket_State_t;

static WebSocket_State_t WebSocket_State;

static const char *TAG = "WebSocket";

/** @brief      Find client by file descriptor.
 *  @param FD   File descriptor
 *  @return     Pointer to client or NULL if not found
 */
static WS_Client_t *WebSocket_FindClient(int FD)
{
    xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (WebSocket_State.Clients[i].Active && WebSocket_State.Clients[i].Fd == FD) {
            return &WebSocket_State.Clients[i];
        }
    }

    xSemaphoreGive(WebSocket_State.ClientsMutex);

    return NULL;
}

/** @brief      Add a new client.
 *  @param FD   File descriptor
 *  @return     Pointer to new client or NULL if full
 */
static WS_Client_t *WebSocket_AddClient(int FD)
{
    xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (WebSocket_State.Clients[i].Active == false) {
            WebSocket_State.Clients[i].Fd = FD;
            WebSocket_State.Clients[i].Active = true;
            WebSocket_State.Clients[i].StreamEnabled = false;
            WebSocket_State.Clients[i].TelemetryEnabled = false;
            WebSocket_State.Clients[i].StreamFormat = IMAGE_FORMAT_JPEG;
            WebSocket_State.Clients[i].StreamFps = 8;
            WebSocket_State.Clients[i].TelemetryIntervalMs = 1000;
            WebSocket_State.Clients[i].LastTelemetryTime = 0;
            WebSocket_State.Clients[i].LastFrameTime = 0;
            WebSocket_State.ClientCount++;

            xSemaphoreGive(WebSocket_State.ClientsMutex);

            ESP_LOGI(TAG, "Client added: fd=%d, total=%d", FD, WebSocket_State.ClientCount);

            return &WebSocket_State.Clients[i];
        }
    }

    xSemaphoreGive(WebSocket_State.ClientsMutex);

    ESP_LOGW(TAG, "Max clients reached, rejecting fd=%d", FD);

    return NULL;
}

/** @brief      Remove a client.
 *  @param FD   File descriptor
 */
static void WebSocket_RemoveClient(int FD)
{
    xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (WebSocket_State.Clients[i].Active && WebSocket_State.Clients[i].Fd == FD) {
            WebSocket_State.Clients[i].Active = false;
            WebSocket_State.ClientCount--;

            ESP_LOGI(TAG, "Client removed: fd=%d, total=%d", FD, WebSocket_State.ClientCount);

            break;
        }
    }

    xSemaphoreGive(WebSocket_State.ClientsMutex);
}

/** @brief          Send JSON message to a client.
 *  @param FD       File descriptor
 *  @param p_Cmd    Command/Event name
 *  @param p_Data   Data JSON object (will not be freed, can be NULL)
 *  @return         ESP_OK on success
 */
esp_err_t WebSocket_SendJSON(int FD, const char *p_Cmd, cJSON *p_Data)
{
    esp_err_t Error;
    char *JSON;
    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "cmd", p_Cmd);

    if (p_Data != NULL) {
        cJSON_AddItemReferenceToObject(msg, "data", p_Data);
    }

    JSON = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    if (JSON == NULL) {
        return ESP_ERR_NO_MEM;
    }

    Frame.payload = reinterpret_cast<uint8_t *>(JSON);
    Frame.len = std::string(JSON).size();

    Error = httpd_ws_send_frame_async(WebSocket_State.ServerHandle, FD, &Frame);

    cJSON_free(JSON);

    return Error;
}

/** @brief          Send binary frame to a client.
 *  @param FD       File descriptor
 *  @param p_Data   Binary data
 *  @param Length   Data length
 *  @return         ESP_OK on success
 */
static esp_err_t WebSocket_SendBinary(int FD, const uint8_t *p_Data, size_t Length)
{
    esp_err_t Error;
    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = const_cast<uint8_t *>(p_Data),
        .len = Length,
    };

    /* Try to send with retry logic to handle queue congestion */
    Error = ESP_FAIL;
    for (uint8_t Retry = 0; Retry < 3; Retry++) {
        Error = httpd_ws_send_frame_async(WebSocket_State.ServerHandle, FD, &Frame);

        if (Error == ESP_OK) {
            /* Send queued successfully - add small delay to prevent queue overflow */
            vTaskDelay(pdMS_TO_TICKS(5));

            break;
        }

        /* Queue might be full, wait and retry */
        ESP_LOGW(TAG, "Failed to queue frame to fd=%d (retry %d): 0x%X!", FD, Retry + 1, Error);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send binary frame to fd=%d after retries: 0x%X (len=%zu)!",
                 FD, Error, Length);
    }

    return Error;
}

/** @brief              Handle start command.
 *  @param p_Client     Client pointer
 *  @param p_Data       Command data
 */
static void WebSocket_HandleStart(WS_Client_t *p_Client, cJSON *p_Data)
{
    cJSON *fps = cJSON_GetObjectItem(p_Data, "fps");

    if (cJSON_IsNumber(fps)) {
        p_Client->StreamFps = static_cast<uint8_t>(fps->valueint);
        if (p_Client->StreamFps < 1) {
            p_Client->StreamFps = 1;
        }
        if (p_Client->StreamFps > 30) {
            p_Client->StreamFps = 30;
        }
    }

    /* Always use JPEG format for simplicity and efficiency */
    p_Client->StreamFormat = IMAGE_FORMAT_JPEG;
    p_Client->StreamEnabled = true;
    p_Client->LastFrameTime = 0;

    ESP_LOGI(TAG, "Stream started for fd=%d, fps=%d", p_Client->Fd, p_Client->StreamFps);

    /* Send ACK */
    cJSON *JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    cJSON_AddNumberToObject(JSON, "fps", p_Client->StreamFps);
    WebSocket_SendJSON(p_Client->Fd, "started", JSON);
    cJSON_Delete(JSON);
}

/** @brief              Handle stop command.
 *  @param p_Client     Client pointer
 */
static void WebSocket_HandleStop(WS_Client_t *p_Client)
{
    cJSON *JSON;

    p_Client->StreamEnabled = false;
    p_Client->LastFrameTime = 0;

    ESP_LOGI(TAG, "Stream stopped for fd=%d", p_Client->Fd);

    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    WebSocket_SendJSON(p_Client->Fd, "stopped", JSON);
    cJSON_Delete(JSON);
}

/** @brief              Handle subscribe command.
 *  @param p_Client     Client pointer
 *  @param p_Data       Command data
 */
static void WebSocket_HandleTelemetrySubscribe(WS_Client_t *p_Client, cJSON *p_Data)
{
    cJSON *JSON;
    cJSON *Interval = cJSON_GetObjectItem(p_Data, "interval");

    if (cJSON_IsNumber(Interval)) {
        p_Client->TelemetryIntervalMs = static_cast<uint32_t>(Interval->valueint);
        if (p_Client->TelemetryIntervalMs < 100) {
            p_Client->TelemetryIntervalMs = 100;
        }
    }

    p_Client->TelemetryEnabled = true;

    ESP_LOGI(TAG, "Telemetry subscribed for fd=%d, interval=%lu ms",
             p_Client->Fd, p_Client->TelemetryIntervalMs);

    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    WebSocket_SendJSON(p_Client->Fd, "subscribed", JSON);
    cJSON_Delete(JSON);
}

/** @brief              Handle unsubscribe command.
 *  @param p_Client     Client pointer
 */
static void WebSocket_HandleTelemetryUnsubscribe(WS_Client_t *p_Client)
{
    cJSON *JSON;

    p_Client->TelemetryEnabled = false;

    ESP_LOGI(TAG, "Telemetry unsubscribed for fd=%d", p_Client->Fd);

    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    WebSocket_SendJSON(p_Client->Fd, "unsubscribed", JSON);
    cJSON_Delete(JSON);
}

/** @brief              Process incoming WebSocket message.
 *  @param p_Client     Client pointer
 *  @param p_Data       Message data
 *  @param length       Message length
 */
static void WebSocket_ProcessMessage(WS_Client_t *p_Client, const char *p_Data, size_t length)
{
    cJSON *JSON;
    cJSON *Cmd;
    cJSON *Data;

    JSON = cJSON_ParseWithLength(p_Data, length);
    if (JSON == NULL) {
        ESP_LOGW(TAG, "Invalid JSON from fd=%d", p_Client->Fd);

        return;
    }

    Cmd = cJSON_GetObjectItem(JSON, "cmd");
    Data = cJSON_GetObjectItem(JSON, "data");

    if (cJSON_IsString(Cmd) == false) {
        ESP_LOGW(TAG, "Invalid message format from fd=%d", p_Client->Fd);

        cJSON_Delete(JSON);

        return;
    }

    /* Handle commands */
    std::string Command(Cmd->valuestring);
    if (Command == "start") {
        WebSocket_HandleStart(p_Client, Data);
    } else if (Command == "stop") {
        WebSocket_HandleStop(p_Client);
    } else if (Command == "subscribe") {
        WebSocket_HandleTelemetrySubscribe(p_Client, Data);
    } else if (Command == "unsubscribe") {
        WebSocket_HandleTelemetryUnsubscribe(p_Client);
    }
    /* Remote Control Commands */
    else if (Command == "get_temperature") {
        WebSocket_Handle_GetTemperature(p_Client->Fd, Data);
    } else if (Command == "get_time") {
        WebSocket_Handle_GetTime(p_Client->Fd, Data);
    } else if (Command == "set_time") {
        WebSocket_Handle_SetTime(p_Client->Fd, Data);
    } else if (Command == "get_battery") {
        WebSocket_Handle_GetBattery(p_Client->Fd, Data);
    } else if (Command == "get_lepton_emissivity") {
        WebSocket_Handle_GetLeptonEmissivity(p_Client->Fd, Data);
    } else if (Command == "set_lepton_emissivity") {
        WebSocket_Handle_SetLeptonEmissivity(p_Client->Fd, Data);
    } else if (Command == "get_lepton_stats") {
        WebSocket_Handle_GetLeptonStats(p_Client->Fd, Data);
    } else if (Command == "get_lepton_roi") {
        WebSocket_Handle_GetLeptonROI(p_Client->Fd, Data);
    } else if (Command == "set_lepton_roi") {
        WebSocket_Handle_SetLeptonROI(p_Client->Fd, Data);
    } else if (Command == "get_lepton_spotmeter") {
        WebSocket_Handle_GetLeptonSpotmeter(p_Client->Fd, Data);
    } else if (Command == "get_flash") {
        WebSocket_Handle_GetFlash(p_Client->Fd, Data);
    } else if (Command == "set_flash") {
        WebSocket_Handle_SetFlash(p_Client->Fd, Data);
    } else if (Command == "get_image_format") {
        WebSocket_Handle_GetImageFormat(p_Client->Fd, Data);
    } else if (Command == "set_image_format") {
        WebSocket_Handle_SetImageFormat(p_Client->Fd, Data);
    } else if (Command == "set_status_led") {
        WebSocket_Handle_SetStatusLED(p_Client->Fd, Data);
    } else if (Command == "get_sd_state") {
        WebSocket_Handle_GetSDState(p_Client->Fd, Data);
    } else if (Command == "format_memory") {
        WebSocket_Handle_FormatMemory(p_Client->Fd, Data);
    } else if (Command == "display_message") {
        WebSocket_Handle_DisplayMessage(p_Client->Fd, Data);
    } else if (Command == "get_lock") {
        WebSocket_Handle_GetLock(p_Client->Fd, Data);
    } else if (Command == "set_lock") {
        WebSocket_Handle_SetLock(p_Client->Fd, Data);
    } else {
        ESP_LOGW(TAG, "Unknown command from fd=%d: %s", p_Client->Fd, Command.c_str());
    }

    cJSON_Delete(JSON);
}

/** @brief              WebSocket handler callback.
 *  @param p_Request    HTTP request pointer
 *  @return             ESP_OK on success, error code on failure
 */
static esp_err_t WebSocket_Handler(httpd_req_t *p_Request)
{
    httpd_ws_frame_t Frame;
    esp_err_t Error;
    WS_Client_t *Client;
    int FD;

    /* Handle new connection */
    if (p_Request->method == HTTP_GET) {
        WS_Client_t *Client;

        Client = WebSocket_AddClient(httpd_req_to_sockfd(p_Request));
        if (Client == NULL) {
            httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "Max clients reached");

            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "WebSocket handshake with fd=%d", Client->Fd);

        return ESP_OK;
    }

    /* Handle WebSocket frame */
    memset(&Frame, 0, sizeof(httpd_ws_frame_t));

    /* Get frame info */
    Error = httpd_ws_recv_frame(p_Request, &Frame, 0);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame info: 0x%X!", Error);

        return Error;
    }

    if (Frame.len > 0) {
        uint32_t Caps;

#ifdef CONFIG_SPIRAM
        Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        Caps = MALLOC_CAP_8BIT;
#endif

        Frame.payload = static_cast<uint8_t *>(heap_caps_malloc(Frame.len + 1, Caps));
        if (Frame.payload == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer!");

            return ESP_ERR_NO_MEM;
        }

        Error = httpd_ws_recv_frame(p_Request, &Frame, Frame.len);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive frame: 0x%X!", Error);

            free(Frame.payload);

            return Error;
        }

        Frame.payload[Frame.len] = '\0';
    }

    FD = httpd_req_to_sockfd(p_Request);
    Client = WebSocket_FindClient(FD);

    switch (Frame.type) {
        case HTTPD_WS_TYPE_TEXT: {
            if (Client != NULL && Frame.payload != NULL) {
                WebSocket_ProcessMessage(Client, (const char *)Frame.payload, Frame.len);
            }

            break;
        }
        case HTTPD_WS_TYPE_CLOSE: {
            ESP_LOGI(TAG, "WebSocket close from fd=%d", FD);
            WebSocket_RemoveClient(FD);

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

            Error = httpd_ws_send_frame_async(WebSocket_State.ServerHandle, FD, &pong_frame);
            if (Error != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send Pong to fd=%d: 0x%X, retrying...", FD, Error);

                /* Pong fails sometimes. So we simply try again */
                vTaskDelay(pdMS_TO_TICKS(10));
                Error = httpd_ws_send_frame_async(WebSocket_State.ServerHandle, FD, &pong_frame);
                if (Error != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send Pong to fd=%d again: 0x%X, removing client!", FD, Error);

                    WebSocket_RemoveClient(FD);
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
    .handler   = WebSocket_Handler,
    .user_ctx  = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true,
    .supported_subprotocol = NULL,
};

esp_err_t WebSocket_Init(void)
{
    if (WebSocket_State.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WebSocket handler");

    memset(WebSocket_State.Clients, 0, sizeof(WebSocket_State.Clients));
    WebSocket_State.ClientCount = 0;
    WebSocket_State.ThermalFrame = NULL;
    WebSocket_State.ServerHandle = NULL;
    WebSocket_State.BroadcastTask = NULL;
    WebSocket_State.FrameReadyQueue = NULL;
    WebSocket_State.IsRunning = false;

    WebSocket_State.ClientsMutex = xSemaphoreCreateMutex();
    if (WebSocket_State.ClientsMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");

        return ESP_ERR_NO_MEM;
    }

    /* Create queue for frame ready notifications (queue size = 1, only latest matters) */
    WebSocket_State.FrameReadyQueue = xQueueCreate(1, sizeof(uint8_t));
    if (WebSocket_State.FrameReadyQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue!");

        vSemaphoreDelete(WebSocket_State.ClientsMutex);

        return ESP_ERR_NO_MEM;
    }

    WebSocket_State.IsInitialized = true;

    return ESP_OK;
}

void WebSocket_Deinit(void)
{
    if (WebSocket_State.IsInitialized == false) {
        return;
    }

    WebSocket_StopTask();

    if (WebSocket_State.FrameReadyQueue != NULL) {
        vQueueDelete(WebSocket_State.FrameReadyQueue);
        WebSocket_State.FrameReadyQueue = NULL;
    }

    if (WebSocket_State.ClientsMutex != NULL) {
        vSemaphoreDelete(WebSocket_State.ClientsMutex);
        WebSocket_State.ClientsMutex = NULL;
    }

    WebSocket_State.IsInitialized = false;

    ESP_LOGI(TAG, "WebSocket handler deinitialized");
}

esp_err_t WebSocket_Register(httpd_handle_t p_ServerHandle)
{
    esp_err_t Error;

    if (WebSocket_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_ServerHandle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    WebSocket_State.ServerHandle = p_ServerHandle;

    Error = httpd_register_uri_handler(p_ServerHandle, &_URI_WebSocket);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WebSocket URI: 0x%X!", Error);

        return Error;
    }

    ESP_LOGI(TAG, "WebSocket handler registered at /ws");

    return ESP_OK;
}

uint8_t WebSocket_GetClientCount(void)
{
    return WebSocket_State.ClientCount;
}

bool WebSocket_HasClients(void)
{
    return WebSocket_State.ClientCount > 0;
}

void WebSocket_SetThermalFrame(Network_Thermal_Frame_t *p_Frame)
{
    xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);
    WebSocket_State.ThermalFrame = p_Frame;
    xSemaphoreGive(WebSocket_State.ClientsMutex);
}

/** @brief          Broadcast task function. Runs in separate task to avoid blocking GUI.
 *  @param p_Param  Task parameter
 */
static void WebSocket_BroadcastTask(void *p_Param)
{
    uint8_t Signal;
    esp_err_t Error;
    Network_Encoded_Image_t Encoded;

    ESP_LOGI(TAG, "WebSocket broadcast task started");

    while (WebSocket_State.IsRunning) {
        /* Wait for frame ready notification (blocking, 100ms timeout) */
        if (xQueueReceive(WebSocket_State.FrameReadyQueue, &Signal, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint32_t Now;

            Now = esp_timer_get_time() / 1000;
            if ((WebSocket_State.IsInitialized == false) || (WebSocket_State.ThermalFrame == NULL) ||
                (WebSocket_State.ClientCount == 0)) {
                continue;
            }

            /* Encode frame ONCE for all clients (assume JPEG format for simplicity) */
            if (xSemaphoreTake(WebSocket_State.ThermalFrame->Mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Error = ImageEncoder_Encode(WebSocket_State.ThermalFrame,
                                            IMAGE_FORMAT_JPEG, PALETTE_IRON, &Encoded);
                xSemaphoreGive(WebSocket_State.ThermalFrame->Mutex);

                if (Error != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to encode frame: 0x%X!", Error);

                    continue;
                }
            } else {
                /* Frame mutex busy, skip this frame */
                continue;
            }

            /* Send to all active streaming clients */
            xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);
            for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
                WS_Client_t *Client;

                Client = &WebSocket_State.Clients[i];
                if ((Client->Active == false) || (Client->StreamEnabled == false)) {
                    continue;
                }

                /* Check frame rate limit */
                if ((Now - Client->LastFrameTime) < (1000 / Client->StreamFps)) {
                    continue;
                }

                /* Copy FD before releasing mutex */
                int ClientFd = Client->Fd;
                uint8_t ClientIdx = i;

                xSemaphoreGive(WebSocket_State.ClientsMutex);
                Error = WebSocket_SendBinary(ClientFd, Encoded.Data, Encoded.Size);
                xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);

                /* Re-validate client is still active and same FD */
                if (WebSocket_State.Clients[ClientIdx].Active &&
                    WebSocket_State.Clients[ClientIdx].Fd == ClientFd) {
                    if (Error == ESP_OK) {
                        WebSocket_State.Clients[ClientIdx].LastFrameTime = Now;
                    } else {
                        ESP_LOGW(TAG, "Removing client fd=%d due to send failure", ClientFd);
                        WebSocket_State.Clients[ClientIdx].Active = false;
                        WebSocket_State.ClientCount--;
                    }
                }
            }

            xSemaphoreGive(WebSocket_State.ClientsMutex);

            /* Free encoded frame after sending to all clients */
            ImageEncoder_Free(&Encoded);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    WebSocket_State.BroadcastTask = NULL;
    vTaskDelete(NULL);
}

esp_err_t WebSocket_NotifyFrameReady(void)
{
    uint8_t Signal;

    if ((WebSocket_State.IsInitialized == false) || (WebSocket_State.FrameReadyQueue == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (WebSocket_State.ClientCount == 0) {
        return ESP_OK;
    }

    /* Signal frame ready (overwrite if queue full - only latest frame matters) */
    Signal = 1;
    xQueueOverwrite(WebSocket_State.FrameReadyQueue, &Signal);

    return ESP_OK;
}

esp_err_t WebSocket_BroadcastTelemetry(void)
{
    uint32_t Now;
    cJSON *JSON;

    if (WebSocket_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (WebSocket_State.ClientCount == 0) {
        return ESP_OK;
    }

    Now = esp_timer_get_time() / 1000;

    /* Build telemetry data */
    JSON = cJSON_CreateObject();

    if (WebSocket_State.ThermalFrame != NULL) {
        // TODO
        //cJSON_AddNumberToObject(JSON, "temp", WebSocket_State.ThermalFrame->temp_avg);
    }

    /* Non-blocking: skip telemetry if mutex is held by another task (called from LVGL timer context) */
    if (xSemaphoreTake(WebSocket_State.ClientsMutex, 0) == pdFALSE) {
        cJSON_Delete(JSON);
        return ESP_ERR_TIMEOUT;
    }

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        WS_Client_t *Client;

        Client = &WebSocket_State.Clients[i];
        if ((Client->Active == false) || (Client->TelemetryEnabled == false)) {
            continue;
        }

        /* Check interval */
        if ((Now - Client->LastTelemetryTime) < Client->TelemetryIntervalMs) {
            continue;
        }

        WebSocket_SendJSON(Client->Fd, "telemetry", JSON);
        Client->LastTelemetryTime = Now;
    }

    xSemaphoreGive(WebSocket_State.ClientsMutex);

    cJSON_Delete(JSON);

    return ESP_OK;
}

esp_err_t WebSocket_PingAll(void)
{
    httpd_ws_frame_t Frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_PING,
        .payload = NULL,
        .len = 0,
    };

    if ((WebSocket_State.IsInitialized == false) || (WebSocket_State.ServerHandle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(WebSocket_State.ClientsMutex, portMAX_DELAY);

    for (uint8_t i = 0; i < CONFIG_NETWORK_WEBSOCKET_CLIENTS; i++) {
        if (WebSocket_State.Clients[i].Active) {
            httpd_ws_send_frame_async(WebSocket_State.ServerHandle, WebSocket_State.Clients[i].Fd, &Frame);
        }
    }

    xSemaphoreGive(WebSocket_State.ClientsMutex);

    return ESP_OK;
}

esp_err_t WebSocket_StartTask(void)
{
    BaseType_t Error;

    if (WebSocket_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (WebSocket_State.BroadcastTask != NULL) {
        ESP_LOGW(TAG, "Broadcast task already running");

        return ESP_OK;
    }

    WebSocket_State.IsRunning = true;

    Error = xTaskCreatePinnedToCore(WebSocket_BroadcastTask, "WS_Broadcast", 4096, NULL, 5, &WebSocket_State.BroadcastTask,
                                    1);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create broadcast task!");
        WebSocket_State.IsRunning = false;

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void WebSocket_StopTask(uint32_t Timeout_ms)
{
    if (WebSocket_State.BroadcastTask == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Stopping WebSocket broadcast task...");

    WebSocket_State.IsRunning = false;
}
