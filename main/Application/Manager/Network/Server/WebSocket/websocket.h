/*
 * websocket.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WebSocket implementation.
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

#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#include <esp_err.h>
#include <esp_http_server.h>
#include <cJSON.h>

#include "../../networkTypes.h"

/** @brief          Initialize the WebSocket.
 *  @param p_Config Pointer to server configuration
 *  @return         ESP_OK on success
 */
esp_err_t WebSocket_Init(const Network_HTTP_Server_Config_t *p_Config);

/** @brief Deinitialize the WebSocket.
 */
void WebSocket_Deinit(void);

/** @brief                  Register WebSocket with HTTP server.
 *  @param p_ServerHandle   HTTP server handle
 *  @return                 ESP_OK on success
 */
esp_err_t WebSocket_Register(httpd_handle_t p_ServerHandle);

/** @brief  Get the number of connected WebSocket clients.
 *  @return Number of connected clients
 */
uint8_t WebSocket_GetClientCount(void);

/** @brief  Check if there are any connected clients.
 *  @return true if at least one client is connected
 */
bool WebSocket_HasClients(void);

/** @brief              Set thermal frame data for streaming.
 *  @param p_Frame      Pointer to thermal frame data
 */
void WebSocket_SetThermalFrame(Network_Thermal_Frame_t *p_Frame);

/** @brief  Signal that a new frame is ready for broadcasting (non-blocking).
 *  @return ESP_OK on success
 */
esp_err_t WebSocket_NotifyFrameReady(void);

/** @brief  Broadcast telemetry data to all subscribed clients.
 *  @return ESP_OK on success
 */
esp_err_t WebSocket_BroadcastTelemetry(void);

/** @brief  Start the WebSocket broadcast task.
 *  @return ESP_OK on success
 */
esp_err_t WebSocket_StartTask(void);

/** @brief              Stop the WebSocket broadcast task.
 *  @param Timeout_ms   Timeout in milliseconds to wait for task to stop (default: 2000ms)
 */
void WebSocket_StopTask(uint32_t Timeout_ms = 2000);

/** @brief  Send ping to all connected clients.
 *  @return ESP_OK on success.
 */
esp_err_t WebSocket_PingAll(void);

#endif /* WEBSOCKET_H_ */
