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
 *  @param p_Config Pointer to WiFi configuration
 *  @return         ESP_OK on success
 */
esp_err_t NetworkManager_Init(Network_WiFi_STA_Config_t *p_Config);

/** @brief Deinitialize the Network Manager.
 */
void NetworkManager_Deinit(void);

/** @brief  Start WiFi in station mode.
 *  @return ESP_OK on success
 */
esp_err_t NetworkManager_StartSTA(void);

/** @brief  Stop Network Manager.
 *  @return ESP_OK on success
 */
esp_err_t NetworkManager_Stop(void);

/** @brief              Connect to WiFi (station mode).
 *  @param p_SSID       SSID to connect to (NULL to use configured SSID)
 *  @param p_Password   Password (NULL to use configured password)
 *  @return             ESP_OK on success
 */
esp_err_t NetworkManager_ConnectWiFi(const char *p_SSID, const char *p_Password);

/** @brief  Disconnect from WiFi.
 *  @return ESP_OK on success
 */
esp_err_t NetworkManager_DisconnectWiFi(void);

/** @brief  Check if WiFi is connected.
 *  @return true if connected
 */
bool NetworkManager_isConnected(void);

/** @brief  Get current WiFi state.
 *  @return Current state
 */
Network_State_t NetworkManager_GetState(void);

/** @brief      Get IP information.
 *  @param p_IP Pointer to store IP info
 *  @return     ESP_OK on success
 */
esp_err_t NetworkManager_GetIP(esp_netif_ip_info_t *p_IP);

/** @brief  Get WiFi signal strength (RSSI).
 *  @return RSSI in dBm, or 0 if not connected
 */
int8_t NetworkManager_GetRSSI(void);

/** @brief          Get MAC address.
 *  @param p_MAC    Buffer to store MAC address (6 bytes)
 *  @return         ESP_OK on success
 */
esp_err_t NetworkManager_GetMAC(uint8_t *p_MAC);

/** @brief                  Set WiFi credentials for station mode.
 *  @param p_Credentials    Pointer to credentials
 *  @return                 ESP_OK on success
 */
esp_err_t NetworkManager_SetCredentials(Network_WiFi_Credentials_t *p_Credentials);

/** @brief  Get number of connected stations (AP mode).
 *  @return Number of connected stations
 */
uint8_t NetworkManager_GetConnectedStations(void);

/** @brief              Start the network server (HTTP + Websocket + VISA).
 *  @param p_Config     Pointer to server configuration
 *  @return             ESP_OK on success
 */
esp_err_t NetworkManager_StartServer(Network_Server_Config_t *p_Config);

#endif /* NETWORKMANAGER_H_ */
