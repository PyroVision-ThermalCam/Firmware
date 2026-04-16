/*
 * visaServer.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA server implementation.
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

#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "visaServer.h"
#include "Settings/settingsManager.h"
#include "Private/visaCommands.h"

/** @brief Maximum command length.
 */
#define VISA_MAX_COMMAND_LENGTH             256

/** @brief Maximum response length.
 */
#define VISA_MAX_RESPONSE_LENGTH            1024

typedef struct {
    int ListenSocket;                   /**< Listening socket */
    uint16_t Port;                      /**< Server port */
    uint16_t Timeout;                   /**< Socket timeout in milliseconds */
    TaskHandle_t ServerTask;            /**< Server task handle */
    bool IsRunning;                     /**< Server running flag */
    bool IsInitialized;                 /**< Initialization flag */
    SemaphoreHandle_t Mutex;            /**< Thread safety mutex */
} VISA_Server_State_t;

static VISA_Server_State_t _VISA_Server_State;

static const char *TAG = "VISA-Server";

/** @brief          Process VISA command and generate response
 *  @param Command  Received command string
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or error code
 */
static int VISA_ProcessCommand(const char *Command, char *Response, size_t MaxLen)
{
    size_t Length;
    std::string Buffer(Command);

    if ((Command == NULL) || (Response == NULL)) {
        return VISA_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Processing command: %s", Command);

    /* Remove trailing newline/carriage return */
    Length = Buffer.size();
    while ((Length > 0) && ((Buffer[Length - 1] == '\n') || (Buffer[Length - 1] == '\r'))) {
        Buffer.resize(--Length);
    }

    return VISACommands_Execute(Buffer.c_str(), Response, MaxLen);
}

/** @brief              Handle client connection.
 *  @param ClientSocket Client socket descriptor
 */
static void VISA_HandleClient(int ClientSocket)
{
    char RxBuffer[VISA_MAX_COMMAND_LENGTH];
    char TxBuffer[VISA_MAX_RESPONSE_LENGTH];
    struct timeval Timeout;

    Timeout.tv_sec = _VISA_Server_State.Timeout / 1000;
    Timeout.tv_usec = (_VISA_Server_State.Timeout % 1000) * 1000;

    setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, &Timeout, sizeof(Timeout));
    setsockopt(ClientSocket, SOL_SOCKET, SO_SNDTIMEO, &Timeout, sizeof(Timeout));

    ESP_LOGI(TAG, "Client connected");

    while (_VISA_Server_State.IsRunning) {
        int Length;

        memset(RxBuffer, 0, sizeof(RxBuffer));

        Length = recv(ClientSocket, RxBuffer, sizeof(RxBuffer) - 1, 0);
        if (Length < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                /* Timeout, continue */
                continue;
            }

            ESP_LOGE(TAG, "recv failed: errno 0x%X!", errno);

            break;
        } else if (Length == 0) {
            ESP_LOGI(TAG, "Client disconnected");

            break;
        }

        RxBuffer[Length] = '\0';
        ESP_LOGD(TAG, "Received: %s", RxBuffer);

        memset(TxBuffer, 0, sizeof(TxBuffer));

        /* Process command */
        Length = VISA_ProcessCommand(RxBuffer, TxBuffer, sizeof(TxBuffer));
        if (Length > 0) {
            int Sent;

            /* Send response */
            Sent = send(ClientSocket, TxBuffer, Length, 0);
            if (Sent < 0) {
                ESP_LOGE(TAG, "send failed: 0x%X!", errno);

                break;
            }

            ESP_LOGD(TAG, "Sent %d bytes", Sent);
        } else if (Length < 0) {
            std::string ErrorStr;

            ErrorStr = "ERROR: " + std::to_string(Length) + "\n";
            send(ClientSocket, ErrorStr.c_str(), ErrorStr.size(), 0);
        }
    }

    close(ClientSocket);
    ESP_LOGI(TAG, "Client connection closed");

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_VISA_CLIENT_DISCONNECTED, NULL, 0, pdMS_TO_TICKS(100));
}

/** @brief          VISA server task.
 *  @param p_Args   Task arguments (unused)
 */
static void Task_VisaServer(void *p_Args)
{
    int Opt = 1;
    int Error;
    struct sockaddr_in Addr;

    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(_VISA_Server_State.Port);

    _VISA_Server_State.ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_VISA_Server_State.ListenSocket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: 0x%X!", errno);

        _VISA_Server_State.IsRunning = false;
        vTaskDelete(NULL);

        return;
    }

    setsockopt(_VISA_Server_State.ListenSocket, SOL_SOCKET, SO_REUSEADDR, &Opt, sizeof(Opt));

    Error = bind(_VISA_Server_State.ListenSocket, (struct sockaddr *)&Addr, sizeof(Addr));
    if (Error != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: 0x%X!", errno);

        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.IsRunning = false;
        vTaskDelete(NULL);

        return;
    }

    Error = listen(_VISA_Server_State.ListenSocket, CONFIG_NETWORK_VISA_MAX_CLIENTS);
    if (Error != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: 0x%X!", errno);

        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.IsRunning = false;
        vTaskDelete(NULL);

        return;
    }

    ESP_LOGI(TAG, "VISA server listening on port %d", _VISA_Server_State.Port);

    while (_VISA_Server_State.IsRunning) {
        struct sockaddr_in Source;
        socklen_t Length = sizeof(Source);
        int Socket;
        std::string Address(16, '\0');

        Socket = accept(_VISA_Server_State.ListenSocket, (struct sockaddr *)&Source, &Length);
        if (Socket < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }

            ESP_LOGE(TAG, "Unable to accept connection: errno 0x%X!", errno);

            break;
        }

        inet_ntop(AF_INET, &Source.sin_addr, &Address[0], Address.size());
        Address.resize(strlen(Address.c_str()));
        ESP_LOGI(TAG, "Client connected from %s:%d", Address.c_str(), ntohs(Source.sin_port));

        esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_VISA_CLIENT_CONNECTED, NULL, 0, pdMS_TO_TICKS(100));

        VISA_HandleClient(Socket);
    }

    close(_VISA_Server_State.ListenSocket);
    _VISA_Server_State.ListenSocket = -1;
    _VISA_Server_State.IsRunning = false;

    ESP_LOGI(TAG, "VISA server stopped");

    vTaskDelete(NULL);
}

esp_err_t VISAServer_Init(void)
{
    esp_err_t Error;
    Settings_VISA_Server_t Config;

    if (_VISA_Server_State.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    SettingsManager_GetVISAServer(&Config);

    memset(&_VISA_Server_State, 0, sizeof(_VISA_Server_State));
    _VISA_Server_State.Port = Config.Port;
    _VISA_Server_State.Timeout = Config.Timeout;

    _VISA_Server_State.Mutex = xSemaphoreCreateMutex();
    if (_VISA_Server_State.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");

        return ESP_ERR_NO_MEM;
    }

    Error = VISACommands_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize command handler: 0x%x!", Error);

        vSemaphoreDelete(_VISA_Server_State.Mutex);

        return Error;
    }

    _VISA_Server_State.ListenSocket = -1;
    _VISA_Server_State.IsInitialized = true;

    ESP_LOGD(TAG, "VISA server initialized");

    return ESP_OK;
}

esp_err_t VISAServer_Deinit(void)
{
    if (_VISA_Server_State.IsInitialized == false) {
        return ESP_OK;
    }

    VISAServer_Stop();
    VISACommands_Deinit();

    if (_VISA_Server_State.Mutex != NULL) {
        vSemaphoreDelete(_VISA_Server_State.Mutex);
        _VISA_Server_State.Mutex = NULL;
    }

    _VISA_Server_State.IsInitialized = false;

    ESP_LOGD(TAG, "VISA server deinitialized");

    return ESP_OK;
}

bool VISAServer_IsRunning(void)
{
    return _VISA_Server_State.IsRunning;
}

esp_err_t VISAServer_Start(void)
{
    BaseType_t Error;

    if (_VISA_Server_State.IsInitialized == false) {
        ESP_LOGE(TAG, "Not initialized!");

        return ESP_ERR_INVALID_STATE;
    } else if (_VISA_Server_State.IsRunning) {
        ESP_LOGW(TAG, "Already running");

        return ESP_OK;
    }

    xSemaphoreTake(_VISA_Server_State.Mutex, portMAX_DELAY);

    _VISA_Server_State.IsRunning = true;

    Error = xTaskCreate(Task_VisaServer, "visa_server", 4096, NULL, 5, &_VISA_Server_State.ServerTask);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task!");

        _VISA_Server_State.IsRunning = false;
        xSemaphoreGive(_VISA_Server_State.Mutex);

        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(_VISA_Server_State.Mutex);

    ESP_LOGD(TAG, "VISA server started");

    return ESP_OK;
}

esp_err_t VISAServer_Stop(void)
{
    if (_VISA_Server_State.IsRunning == false) {
        return ESP_OK;
    }

    xSemaphoreTake(_VISA_Server_State.Mutex, portMAX_DELAY);

    _VISA_Server_State.IsRunning = false;

    if (_VISA_Server_State.ListenSocket >= 0) {
        shutdown(_VISA_Server_State.ListenSocket, SHUT_RDWR);
        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.ListenSocket = -1;
    }

    xSemaphoreGive(_VISA_Server_State.Mutex);

    ESP_LOGD(TAG, "VISA server stopped");

    return ESP_OK;
}
