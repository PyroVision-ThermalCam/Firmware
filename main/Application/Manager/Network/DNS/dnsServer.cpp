/*
 * dnsServer.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Simple DNS server implementation for captive portal.
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
#include <esp_netif.h>
#include <esp_task_wdt.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string.h>

#include "dnsServer.h"

#define DNS_PORT            53
#define DNS_BUFFER_SIZE     512

/** @brief DNS header structure.
 */
typedef struct {
    uint16_t ID;                            /**< Identification */
    uint16_t Flags;                         /**< Flags */
    uint16_t QDCount;                       /**< Number of questions */
    uint16_t ANCount;                       /**< Number of answer RRs */
    uint16_t NSCount;                       /**< Number of authority RRs */
    uint16_t ARCount;                       /**< Number of additional RRs */
}  __attribute__((packed)) DNS_Header_t;

typedef struct {
    bool IsRunning;
    int Socket;
    TaskHandle_t Task;
} DNS_Server_State_t;

static DNS_Server_State_t _DNS_Server_State;

static const char *TAG = "DNS-Server";

/** @brief          DNS server task.
 *  @param p_Arg    Task argument (not used)
 */
static void DNS_Server_Task(void *p_Arg)
{
    uint8_t Buffer[DNS_BUFFER_SIZE];
    struct sockaddr_in ClientAddr;
    socklen_t ClientAddrLen = sizeof(ClientAddr);
    esp_netif_ip_info_t IP_Info;
    esp_netif_t *AP_NetIF;

    _DNS_Server_State.IsRunning = true;

    /* Get AP IP address */
    AP_NetIF = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (AP_NetIF == NULL) {
        ESP_LOGE(TAG, "Failed to get AP netif!");

        vTaskDelete(NULL);

        return;
    }

    esp_netif_get_ip_info(AP_NetIF, &IP_Info);

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "DNS server started, redirecting all queries to " IPSTR, IP2STR(&IP_Info.ip));

    while (_DNS_Server_State.IsRunning) {
        DNS_Header_t *Header;
        int Length;
        uint16_t *AnswerPtr;
        uint8_t *ResponsePtr;

        esp_task_wdt_reset();

        Length = recvfrom(_DNS_Server_State.Socket, Buffer, sizeof(Buffer), 0,
                          (struct sockaddr *)&ClientAddr, &ClientAddrLen);

        /* Timeout or no data - continue loop to reset watchdog */
        if (Length < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }

            ESP_LOGW(TAG, "recvfrom error: 0x%X", errno);

            continue;
        }

        if (Length < sizeof(DNS_Header_t)) {
            continue;
        }

        Header = reinterpret_cast<DNS_Header_t *>(Buffer);
        Header->Flags = htons(0x8180);
        Header->ANCount = Header->QDCount;
        Header->NSCount = 0;
        Header->ARCount = 0;

        ResponsePtr = Buffer + sizeof(DNS_Header_t);
        while (ResponsePtr < (Buffer + Length) && (*ResponsePtr != 0)) {
            ResponsePtr += *ResponsePtr + 1;
        }
        ResponsePtr += 5;

        AnswerPtr = reinterpret_cast<uint16_t *>(ResponsePtr);
        *AnswerPtr++ = htons(0xC00C);   /* Name pointer to question */
        *AnswerPtr++ = htons(0x0001);   /* Type A */
        *AnswerPtr++ = htons(0x0001);   /* Class IN */
        *AnswerPtr++ = htons(0x0000);   /* TTL high */
        *AnswerPtr++ = htons(0x003C);   /* TTL low (60 seconds) */
        *AnswerPtr++ = htons(0x0004);   /* Data length */

        __builtin_memcpy(AnswerPtr, &IP_Info.ip.addr, sizeof(IP_Info.ip.addr));
        ResponsePtr = reinterpret_cast<uint8_t *>(AnswerPtr) + sizeof(IP_Info.ip.addr);

        sendto(_DNS_Server_State.Socket, Buffer, ResponsePtr - Buffer, 0,
               (struct sockaddr *)&ClientAddr, ClientAddrLen);

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "DNS server task exiting");

    esp_task_wdt_delete(NULL);

    vTaskDelete(NULL);
}

esp_err_t DNS_Server_Start(void)
{
    struct sockaddr_in ServerAddr;

    if (_DNS_Server_State.IsRunning) {
        ESP_LOGW(TAG, "DNS server already running");

        return ESP_OK;
    }

    _DNS_Server_State.Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_DNS_Server_State.Socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: 0x%X!", errno);

        return ESP_FAIL;
    }

    /* Set socket timeout to allow watchdog reset */
    struct timeval Timeout;
    Timeout.tv_sec = 1;
    Timeout.tv_usec = 0;
    if (setsockopt(_DNS_Server_State.Socket, SOL_SOCKET, SO_RCVTIMEO, &Timeout, sizeof(Timeout)) < 0) {
        ESP_LOGW(TAG, "Failed to set socket timeout: 0x%X!", errno);
    }

    __builtin_memset(&ServerAddr, 0, sizeof(ServerAddr));
    ServerAddr.sin_family = AF_INET;
    ServerAddr.sin_addr.s_addr = INADDR_ANY;
    ServerAddr.sin_port = htons(DNS_PORT);

    if (bind(_DNS_Server_State.Socket, (struct sockaddr *)&ServerAddr, sizeof(ServerAddr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: 0x%X!", errno);

        close(_DNS_Server_State.Socket);

        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(DNS_Server_Task, "DNS_Server", 4096, NULL, 3, &_DNS_Server_State.Task, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task!");

        close(_DNS_Server_State.Socket);
        _DNS_Server_State.IsRunning = false;

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "DNS server initialized");

    return ESP_OK;
}

void DNS_Server_Stop(void)
{
    if (_DNS_Server_State.IsRunning == false) {
        return;
    }

    _DNS_Server_State.IsRunning = false;

    if (_DNS_Server_State.Socket >= 0) {
        close(_DNS_Server_State.Socket);
        _DNS_Server_State.Socket = -1;
    }

    ESP_LOGD(TAG, "DNS server stopped");
}
