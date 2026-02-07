/*
 * networkManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network Manager definition.
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

#ifndef NETWORKMANAGER_H_
#define NETWORKMANAGER_H_

#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>

#include "networkTypes.h"
#include "SNTP/sntp.h"
#include "Server/server.h"
#include "Provisioning/provisioning.h"

/** @brief          Initialize the Network Manager.
 *                  Initializes WiFi subsystem, creates network interfaces (STA and AP),
 *                  and sets up event handlers. Must be called before any network operations.
 *  @note           This function must be called after NVS and event loop init.
 *                  WiFi is not started automatically - call NetworkManager_StartSTA().
 *  @warning        Not thread-safe during initialization.
 *  @param p_Config Pointer to WiFi station configuration (SSID, password)
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_FAIL if WiFi initialization fails
 */
esp_err_t NetworkManager_Init(Network_WiFi_STA_Config_t *p_Config);

/** @brief          Deinitialize the Network Manager.
 *                  Stops WiFi, disconnects all connections, stops servers, and cleans up
 *                  network interfaces. All network handles become invalid.
 *  @note           Automatically disconnects from WiFi if connected.
 *                  Stops HTTP server, WebSocket, and VISA server if running.
 *  @warning        Cannot be undone without calling NetworkManager_Init() again.
 */
void NetworkManager_Deinit(void);

/** @brief          Start WiFi in station mode.
 *                  Enables WiFi in station (client) mode and attempts to connect to
 *                  the configured access point.
 *  @note           Connection happens asynchronously - check NetworkManager_isConnected().
 *                  Posts NETWORK_EVENT_WIFI_CONNECTING event.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 *                  ESP_FAIL if WiFi start fails
 */
esp_err_t NetworkManager_StartSTA(void);

/** @brief          Stop Network Manager.
 *                  Stops WiFi radio and disconnects from network. Does not deinitialize
 *                  the manager - use NetworkManager_StartSTA() to restart.
 *  @note           Servers are stopped but not destroyed.
 *                  Posts NETWORK_EVENT_WIFI_DISCONNECTED event.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if stop operation fails
 */
esp_err_t NetworkManager_Stop(void);

/** @brief              Connect to WiFi access point (station mode).
 *                      Attempts to connect to specified access point. Connection happens
 *                      asynchronously - use NetworkManager_isConnected() to check status.
 *  @note               Maximum retry attempts defined in configuration.
 *                      Posts NETWORK_EVENT_WIFI_CONNECTED on success.
 *                      If NULL parameters, uses credentials from NetworkManager_Init().
 *  @param p_SSID       SSID to connect to (NULL to use configured SSID)
 *  @param p_Password   Password (NULL to use configured password)
 *  @return             ESP_OK if connection attempt started
 *                      ESP_ERR_INVALID_STATE if not initialized or already connected
 *                      ESP_ERR_INVALID_ARG if SSID is too long (max 32 chars)
 *                      ESP_FAIL if connection initiation fails
 */
esp_err_t NetworkManager_ConnectWiFi(const char *p_SSID, const char *p_Password);

/** @brief          Disconnect from WiFi.
 *                  Disconnects from current WiFi access point. Keeps WiFi radio active.
 *  @note           Posts NETWORK_EVENT_WIFI_DISCONNECTED event.
 *                  To stop WiFi completely, use NetworkManager_Stop().
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not connected
 *                  ESP_FAIL if disconnect operation fails
 */
esp_err_t NetworkManager_DisconnectWiFi(void);

/** @brief          Check if WiFi is connected.
 *                  Returns the current WiFi connection status. Returns true only when
 *                  fully connected with valid IP address.
 *  @note           This is thread-safe and can be called from any task.
 *  @return         true if connected to WiFi with IP address
 *                  false if disconnected, connecting, or no IP
 */
bool NetworkManager_isConnected(void);

/** @brief          Get current WiFi state.
 *                  Returns detailed connection state including disconnected, connecting,
 *                  connected, provisioning, etc.
 *  @note           Thread-safe.
 *                  Use NetworkManager_isConnected() for simple connected check.
 *  @return         Current network state (see Network_State_t)
 */
Network_State_t NetworkManager_GetState(void);

/** @brief          Get IP information.
 *                  Retrieves current IP address, netmask, and gateway for the station
 *                  interface.
 *  @note           Only valid when WiFi is connected.
 *  @param p_IP     Pointer to esp_netif_ip_info_t structure to fill
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_IP is NULL
 *                  ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t NetworkManager_GetIP(esp_netif_ip_info_t *p_IP);

/** @brief          Get WiFi signal strength (RSSI).
 *                  Returns the Received Signal Strength Indicator in dBm.
 *                  Typical values: -30 dBm (excellent) to -90 dBm (poor).
 *  @note           Only valid when connected to WiFi.
 *                  Value updates periodically while connected.
 *  @return         RSSI in dBm (negative value)
 *                  0 if not connected or error
 */
int8_t NetworkManager_GetRSSI(void);

/** @brief          Get MAC address of WiFi station interface.
 *                  Retrieves the 6-byte hardware MAC address.
 *  @note           MAC address is factory-programmed in ESP32 eFuse.
 *  @param p_MAC    Buffer to store MAC address (must be at least 6 bytes)
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_MAC is NULL
 *                  ESP_FAIL if MAC read fails
 */
esp_err_t NetworkManager_GetMAC(uint8_t *p_MAC);

/** @brief                  Set WiFi credentials for station mode.
 *                          Updates stored WiFi credentials. Changes take effect on next
 *                          NetworkManager_ConnectWiFi() call.
 *  @note                   Does not automatically reconnect - call NetworkManager_ConnectWiFi().
 *                          SSID max: 32 chars, Password max: 64 chars.
 *  @param p_Credentials    Pointer to credentials structure (SSID, password)
 *  @return                 ESP_OK on success
 *                          ESP_ERR_INVALID_ARG if p_Credentials is NULL
 *                          ESP_ERR_INVALID_ARG if SSID or password too long
 */
esp_err_t NetworkManager_SetCredentials(Network_WiFi_Credentials_t *p_Credentials);

/** @brief          Get number of connected stations (AP mode).
 *                  Returns the number of client devices currently connected to the
 *                  soft AP (Access Point) interface.
 *  @note           Only relevant when operating in AP or AP+STA mode.
 *                  Returns 0 in pure STA mode.
 *  @return         Number of connected stations (0-4 typical limit)
 */
uint8_t NetworkManager_GetConnectedStations(void);

/** @brief              Start the network server (HTTP + WebSocket + VISA).
 *                      Starts HTTP server with WebSocket support and VISA/SCPI server.
 *                      Servers listen on configured ports and handle client connections.
 *  @note               HTTP server default port: 80, VISA default: 5025.
 *                      WebSocket integrated into HTTP server.
 *                      Can be called before or after WiFi connection.
 *  @param p_Config     Pointer to server configuration (ports, max clients)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Config is NULL
 *                      ESP_ERR_INVALID_STATE if network not initialized
 *                      ESP_ERR_NO_MEM if memory allocation fails
 *                      ESP_FAIL if server start fails
 */
esp_err_t NetworkManager_StartServer(Network_Server_Config_t *p_Config);

#endif /* NETWORKMANAGER_H_ */
