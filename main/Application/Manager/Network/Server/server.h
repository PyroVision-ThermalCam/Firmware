/*
 * server.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Server module header.
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

#ifndef SERVER_H_
#define SERVER_H_

#include "HTTP/http_server.h"
#include "WebSocket/websocket.h"
#include "VISA/visaServer.h"
#include "ImageEncoder/imageEncoder.h"

/** @brief          Initialize the complete server (HTTP + WebSocket + Image Encoder + VISA).
 *  @param p_Config Pointer to server configuration
 *  @return         ESP_OK on success
 */
static inline esp_err_t Server_Init(const Network_Server_Config_t *p_Config)
{
    esp_err_t Error;

    /* Default JPEG quality */
    Error = ImageEncoder_Init(80);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = HTTP_Server_Init(&p_Config->HTTP_Server);
    if (Error != ESP_OK) {
        ImageEncoder_Deinit();
        return Error;
    }

    Error = WebSocket_Init(&p_Config->HTTP_Server);
    if (Error != ESP_OK) {
        HTTP_Server_Deinit();
        ImageEncoder_Deinit();
        return Error;
    }

    Error = VISAServer_Init(&p_Config->VISA_Server);
    if (Error != ESP_OK) {
        WebSocket_Deinit();
        HTTP_Server_Deinit();
        ImageEncoder_Deinit();

        return Error;
    }

    return ESP_OK;
}

/** @brief Deinitialize the complete server.
 */
static inline void Server_Deinit(void)
{
    VISAServer_Deinit();
    WebSocket_Deinit();
    HTTP_Server_Deinit();
    ImageEncoder_Deinit();
}

/** @brief  Start the server (HTTP server and register WebSocket handler).
 *  @return ESP_OK on success
 */
static inline esp_err_t Server_Start(void)
{
    esp_err_t Error;

    Error = HTTP_Server_Start();
    if (Error != ESP_OK) {
        return Error;
    }

    Error = WebSocket_Register(HTTP_Server_GetHandle());
    if (Error != ESP_OK) {
        HTTP_Server_Stop();
        return Error;
    }

    Error = WebSocket_StartTask();
    if (Error != ESP_OK) {
        HTTP_Server_Stop();
        return Error;
    }

    return ESP_OK;
}

/** @brief  Stop the server.
 *  @return ESP_OK on success
 */
static inline esp_err_t Server_Stop(void)
{
    WebSocket_StopTask();

    return HTTP_Server_Stop();
}

/** @brief  Check if the server is running.
 *  @return true if running
 */
static inline bool Server_isRunning(void)
{
    return HTTP_Server_isRunning();
}

/** @brief          Set the thermal frame data for both HTTP and WebSocket endpoints.
 *  @param p_Frame  Pointer to thermal frame data
 */
static inline void Server_SetThermalFrame(Network_Thermal_Frame_t *p_Frame)
{
    HTTP_Server_SetThermalFrame(p_Frame);
    WebSocket_SetThermalFrame(p_Frame);
}

/** @brief Notify all clients that a new frame is ready (non-blocking).
 */
static inline void Server_NotifyClients(void)
{
    if (WebSocket_HasClients()) {
        WebSocket_NotifyFrameReady();
    }
}

#endif /* SERVER_H_ */
