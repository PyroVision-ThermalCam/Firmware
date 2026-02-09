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

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "visaServer.h"
#include "Private/visaCommands.h"

typedef struct {
    int ListenSocket;                   /**< Listening socket */
    TaskHandle_t ServerTask;            /**< Server task handle */
    bool isRunning;                     /**< Server running flag */
    bool isInitialized;                 /**< Initialization flag */
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
    char cmd_buffer[VISA_MAX_COMMAND_LENGTH];

    if ((Command == NULL) || (Response == NULL)) {
        return VISA_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Processing command: %s", Command);

    /* Remove trailing newline/carriage return */
    strncpy(cmd_buffer, Command, sizeof(cmd_buffer) - 1);
    cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';

    size_t len = strlen(cmd_buffer);
    while ((len > 0) && ((cmd_buffer[len - 1] == '\n') || (cmd_buffer[len - 1] == '\r'))) {
        cmd_buffer[--len] = '\0';
    }

    return VISACommands_Execute(cmd_buffer, Response, MaxLen);
}

/** @brief              Handle client connection.
 *  @param ClientSocket Client socket descriptor
 */
static void VISA_HandleClient(int ClientSocket)
{
    char rx_buffer[VISA_MAX_COMMAND_LENGTH];
    char tx_buffer[VISA_MAX_RESPONSE_LENGTH];
    struct timeval timeout;

    timeout.tv_sec = VISA_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (VISA_SOCKET_TIMEOUT_MS % 1000) * 1000;

    setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(ClientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "Client connected");

    while (_VISA_Server_State.isRunning) {
        int len;

        memset(rx_buffer, 0, sizeof(rx_buffer));

        len = recv(ClientSocket, rx_buffer, sizeof(rx_buffer) - 1, 0);

        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                /* Timeout, continue */
                continue;
            }

            ESP_LOGE(TAG, "recv failed: errno %d!", errno);

            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "Client disconnected");

            break;
        }

        rx_buffer[len] = '\0';
        ESP_LOGD(TAG, "Received: %s", rx_buffer);

        /* Process command */
        memset(tx_buffer, 0, sizeof(tx_buffer));
        int response_len = VISA_ProcessCommand(rx_buffer, tx_buffer, sizeof(tx_buffer));

        if (response_len > 0) {
            int sent;

            /* Send response */
            sent = send(ClientSocket, tx_buffer, response_len, 0);
            if (sent < 0) {
                ESP_LOGE(TAG, "send failed: %d!", errno);

                break;
            }

            ESP_LOGD(TAG, "Sent %d bytes", sent);
        } else if (response_len < 0) {
            /* Error response */
            snprintf(tx_buffer, sizeof(tx_buffer), "ERROR: %d\n", response_len);
            send(ClientSocket, tx_buffer, strlen(tx_buffer), 0);
        }
    }

    close(ClientSocket);
    ESP_LOGI(TAG, "Client connection closed");
}

/** @brief          VISA server task.
 *  @param p_Args   Task arguments (unused)
 */
static void VISA_ServerTask(void *p_Args)
{
    int opt = 1;
    int Error;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(VISA_SERVER_PORT);

    _VISA_Server_State.ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_VISA_Server_State.ListenSocket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: %d!", errno);

        _VISA_Server_State.isRunning = false;
        vTaskDelete(NULL);

        return;
    }

    setsockopt(_VISA_Server_State.ListenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    Error = bind(_VISA_Server_State.ListenSocket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (Error != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: %d!", errno);

        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.isRunning = false;
        vTaskDelete(NULL);

        return;
    }

    Error = listen(_VISA_Server_State.ListenSocket, VISA_MAX_CLIENTS);
    if (Error != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: %d!", errno);

        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.isRunning = false;
        vTaskDelete(NULL);

        return;
    }

    ESP_LOGI(TAG, "VISA server listening on port %d", VISA_SERVER_PORT);

    while (_VISA_Server_State.isRunning) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int Socket;
        char addr_str[16];

        Socket = accept(_VISA_Server_State.ListenSocket, (struct sockaddr *)&source_addr, &addr_len);

        if (Socket < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }

            ESP_LOGE(TAG, "Unable to accept connection: errno %d!", errno);

            break;
        }

        inet_ntop(AF_INET, &source_addr.sin_addr, addr_str, sizeof(addr_str));
        ESP_LOGI(TAG, "Client connected from %s:%d", addr_str, ntohs(source_addr.sin_port));

        VISA_HandleClient(Socket);
    }

    close(_VISA_Server_State.ListenSocket);
    _VISA_Server_State.ListenSocket = -1;
    _VISA_Server_State.isRunning = false;

    ESP_LOGI(TAG, "VISA server stopped");
    vTaskDelete(NULL);
}

esp_err_t VISAServer_Init(const Network_VISA_Server_Config_t *p_Config)
{
    esp_err_t Error;

    if (_VISA_Server_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    memset(&_VISA_Server_State, 0, sizeof(_VISA_Server_State));

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
    _VISA_Server_State.isInitialized = true;

    ESP_LOGI(TAG, "VISA server initialized");

    return ESP_OK;
}

esp_err_t VISAServer_Deinit(void)
{
    if (_VISA_Server_State.isInitialized == false) {
        return ESP_OK;
    }

    VISAServer_Stop();

    VISACommands_Deinit();

    if (_VISA_Server_State.Mutex != NULL) {
        vSemaphoreDelete(_VISA_Server_State.Mutex);
        _VISA_Server_State.Mutex = NULL;
    }

    _VISA_Server_State.isInitialized = false;

    ESP_LOGI(TAG, "VISA server deinitialized");

    return ESP_OK;
}

bool VISAServer_IsRunning(void)
{
    return _VISA_Server_State.isRunning;
}

esp_err_t VISAServer_Start(void)
{
    if (_VISA_Server_State.isInitialized == false) {
        ESP_LOGE(TAG, "Not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    if (_VISA_Server_State.isRunning) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    xSemaphoreTake(_VISA_Server_State.Mutex, portMAX_DELAY);

    _VISA_Server_State.isRunning = true;

    BaseType_t result = xTaskCreate(
                            VISA_ServerTask,
                            "visa_server",
                            4096,
                            NULL,
                            5,
                            &_VISA_Server_State.ServerTask
                        );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task!");
        _VISA_Server_State.isRunning = false;
        xSemaphoreGive(_VISA_Server_State.Mutex);

        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(_VISA_Server_State.Mutex);

    ESP_LOGI(TAG, "VISA server started");

    return ESP_OK;
}

esp_err_t VISAServer_Stop(void)
{
    if (_VISA_Server_State.isRunning == false) {
        return ESP_OK;
    }

    xSemaphoreTake(_VISA_Server_State.Mutex, portMAX_DELAY);

    _VISA_Server_State.isRunning = false;

    if (_VISA_Server_State.ListenSocket >= 0) {
        shutdown(_VISA_Server_State.ListenSocket, SHUT_RDWR);
        close(_VISA_Server_State.ListenSocket);
        _VISA_Server_State.ListenSocket = -1;
    }

    /* Wait for task to terminate */
    if (_VISA_Server_State.ServerTask != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _VISA_Server_State.ServerTask = NULL;
    }

    xSemaphoreGive(_VISA_Server_State.Mutex);

    ESP_LOGI(TAG, "VISA server stopped");

    return ESP_OK;
}
