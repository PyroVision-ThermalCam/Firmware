/*
 * networkTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the Network Manager component.
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

#ifndef NETWORK_TYPES_H_
#define NETWORK_TYPES_H_

#include <esp_err.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "Settings/settingsTypes.h"

#define NETWORK_ERR_BASE                    0x3000

/** @defgroup NETWORK_ERRORS Network Manager Error Codes
 *  @brief Error codes returned by NetworkManager functions (base: @c NETWORK_ERR_BASE = 0x3000).
 *  @{
 */

/** @brief NetworkManager_Init() has not been called yet. */
#define NETWORK_ERR_NOT_INITIALIZED             (NETWORK_ERR_BASE + 0x01)

/** @brief WiFi subsystem (esp_wifi_init) initialisation failed. */
#define NETWORK_ERR_WIFI_INIT                   (NETWORK_ERR_BASE + 0x02)

/** @brief Starting WiFi in station mode (esp_wifi_start) failed. */
#define NETWORK_ERR_WIFI_START                  (NETWORK_ERR_BASE + 0x03)

/** @brief WiFi is not connected — operation requires an active WiFi connection. */
#define NETWORK_ERR_NOT_CONNECTED               (NETWORK_ERR_BASE + 0x04)

/** @brief WiFi disconnect operation failed. */
#define NETWORK_ERR_DISCONNECT                  (NETWORK_ERR_BASE + 0x05)

/** @brief Network interface (esp_netif) creation failed. */
#define NETWORK_ERR_NETIF_CREATE                (NETWORK_ERR_BASE + 0x06)

/** @brief HTTP / WebSocket server start failed. */
#define NETWORK_ERR_SERVER_START                (NETWORK_ERR_BASE + 0x07)

/** @brief HTTP / WebSocket server stop failed. */
#define NETWORK_ERR_SERVER_STOP                 (NETWORK_ERR_BASE + 0x08)

/** @brief SNTP initialisation or start failed. */
#define NETWORK_ERR_SNTP_INIT                   (NETWORK_ERR_BASE + 0x09)

/** @brief Provisioning start failed. */
#define NETWORK_ERR_PROV_START                  (NETWORK_ERR_BASE + 0x0A)

/** @brief Provisioning timeout — no credentials received within the timeout window. */
#define NETWORK_ERR_PROV_TIMEOUT                (NETWORK_ERR_BASE + 0x0B)

/** @brief OTA firmware update failed. */
#define NETWORK_ERR_OTA_FAILED                  (NETWORK_ERR_BASE + 0x0C)

/** @} */

/** @brief Network Manager events base.
 */
ESP_EVENT_DECLARE_BASE(NETWORK_EVENTS);

/** @brief Network connection state.
 */
typedef enum {
    NETWORK_STATE_IDLE = 0,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_PROVISIONING,
    NETWORK_STATE_AP_STARTED,
    NETWORK_STATE_ERROR,
} Network_State_t;

/** @brief Provisioning method.
 */
typedef enum {
    NETWORK_PROV_NONE = 0,
    NETWORK_PROV_BLE,
    NETWORK_PROV_SOFTAP,
    NETWORK_PROV_BOTH,
} Network_ProvMethod_t;

/** @brief Network event types (used as event IDs in NETWORK_EVENTS base).
 */
typedef enum {
    NETWORK_EVENT_WIFI_CONNECTED = 0,
    NETWORK_EVENT_WIFI_DISCONNECTED,
    NETWORK_EVENT_WIFI_GOT_IP,                  /**< The device got an IP address
                                                     Data is of type Network_IP_Info_t */
    NETWORK_EVENT_VISA_CLIENT_CONNECTED,        /**< A client connected to the VISA server */
    NETWORK_EVENT_VISA_CLIENT_DISCONNECTED,     /**< A client disconnected from the VISA server */
    NETWORK_EVENT_REMOTE_LOCK_SET,              /**< Set the remote lock state.
                                                     Data is of type bool. */
    NETWORK_EVENT_REMOTE_DISPLAY_MESSAGE,       /**< Remote display message changed.
                                                     Data is of type Remote_Display_Message_t. */
    NETWORK_EVENT_AP_STARTED,
    NETWORK_EVENT_AP_STOPPED,
    NETWORK_EVENT_AP_STA_CONNECTED,
    NETWORK_EVENT_AP_STA_DISCONNECTED,
    NETWORK_EVENT_PROV_STARTED,
    NETWORK_EVENT_PROV_STOPPED,
    NETWORK_EVENT_PROV_CRED_RECV,
    NETWORK_EVENT_PROV_SUCCESS,
    NETWORK_EVENT_PROV_FAILED,
    NETWORK_EVENT_PROV_TIMEOUT,
    NETWORK_EVENT_OTA_STARTED,
    NETWORK_EVENT_OTA_PROGRESS,
    NETWORK_EVENT_OTA_COMPLETED,
    NETWORK_EVENT_OTA_FAILED,
    NETWORK_EVENT_OPEN_WIFI_REQUEST,            /**< Request to open a WiFi connection */
    NETWORK_EVENT_SERVER_STARTED,               /**< HTTP/WebSocket server started */
    NETWORK_EVENT_SERVER_STOPPED,               /**< HTTP/WebSocket server stopped */
    NETWORK_EVENT_SERVER_ERROR,                 /**< HTTP/WebSocket server error */
} Network_Event_t;

/** @brief Scale mode for temperature visualization.
 */
typedef enum {
    SCALE_LINEAR = 0,
    SCALE_HISTOGRAM,
} Server_Scale_t;

/** @brief LED state.
 */
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_BLINK,
} Server_LED_State_t;

/** @brief WebSocket message types.
 */
typedef enum {
    WS_MSG_TYPE_EVENT = 0,
    WS_MSG_TYPE_COMMAND,
    WS_MSG_TYPE_RESPONSE,
} Server_WS_Message_Type_t;

/** @brief Remote control display message structure.
 */
typedef struct {
    char Message[128];                    /**< Message to display on the device */
} Remote_Display_Message_t;

/** @brief LED color enumeration.
 */
typedef enum {
    REMOTE_LED_RED = 0,         /**< Red LED. */
    REMOTE_LED_GREEN = 1,       /**< Green LED. */
    REMOTE_LED_BLUE = 2,        /**< Blue LED. */
} Remote_LED_Color_t;

/** @brief Thermal telemetry data structure.
 */
typedef struct {
} Network_Thermal_Telemetry_t;

/** @brief IP info event data (for NETWORK_EVENT_WIFI_GOT_IP).
 */
typedef struct {
    uint32_t IP;
    uint32_t Netmask;
    uint32_t Gateway;
} Network_IP_Info_t;

/** @brief Station info event data (for AP_STA_CONNECTED/DISCONNECTED).
 */
typedef struct {
    uint8_t MAC[6];
} Network_Event_STA_Info_t;

/** @brief HTTP Server configuration.
 */
typedef struct {
    uint16_t Port;
    uint8_t MaxClients;
    uint16_t WSPingIntervalSec;
    bool EnableCORS;
    char API_Key[64];
} Network_HTTP_Server_Config_t;

#endif /* NETWORK_TYPES_H_ */
