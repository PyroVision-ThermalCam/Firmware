/*
 * http_server.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP server handler.
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

#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <esp_err.h>
#include <esp_http_server.h>

#include "../networkTypes.h"

/** @brief          Initialize the HTTP server.
 *  @param p_Config Pointer to server configuration.
 *  @return         ESP_OK on success.
 */
esp_err_t HTTP_Server_Init(const Network_HTTP_Server_Config_t *p_Config);

/** @brief Deinitialize the HTTP server.
 */
void HTTP_Server_Deinit(void);

/** @brief  Start the HTTP server.
 *  @return ESP_OK on success
 */
esp_err_t HTTP_Server_Start(void);

/** @brief  Stop the HTTP server.
 *  @return ESP_OK on success
 */
esp_err_t HTTP_Server_Stop(void);

/** @brief  Check if the HTTP server is running.
 *  @return true if running
 */
bool HTTP_Server_isRunning(void);

/** @brief          Set thermal frame data for image endpoint.
 *  @param p_Frame  Pointer to thermal frame data.
 */
void HTTP_Server_SetThermalFrame(Network_Thermal_Frame_t *p_Frame);

/** @brief  Get the HTTP server handle for WebSocket registration.
 *  @return HTTP server handle or NULL if not running
 */
httpd_handle_t HTTP_Server_GetHandle(void);

#endif /* HTTP_SERVER_H_ */
