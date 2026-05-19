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
#include "Settings/settingsManager.h"

/** @brief          Initialize the complete server (HTTP + WebSocket + Image Encoder + VISA).
 *  @return         ESP_OK on success
 */
static inline esp_err_t Server_Init(void)
{
    esp_err_t Error;
    Settings_HTTP_Server_t Config;
    Network_HTTP_Server_Config_t ServerConfig;

    SettingsManager_GetHTTPServer(&Config);
    __builtin_memcpy(ServerConfig.API_Key, Config.APIKey, sizeof(ServerConfig.API_Key));
    ServerConfig.EnableCORS = Config.UseCORS;
    ServerConfig.MaxClients = Config.MaxClients;
    ServerConfig.Port = Config.Port;
    ServerConfig.WSPingIntervalSec = Config.WSPingIntervalSec;

    Error = HTTP_Server_Init(&ServerConfig);
    if (Error != ESP_OK) {
        ImageEncoder_Deinit();

        return Error;
    }

    Error = WebSocket_Init();
    if (Error != ESP_OK) {
        HTTP_Server_Deinit();
        ImageEncoder_Deinit();

        return Error;
    }

    Error = VISAServer_Init();
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

    Error = VISAServer_Start();
    if (Error != ESP_OK) {
        WebSocket_StopTask();
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
    VISAServer_Stop();
    WebSocket_StopTask();

    return HTTP_Server_Stop();
}

/** @brief  Check if the server is running.
 *  @return true if running
 */
static inline bool Server_IsRunning(void)
{
    return HTTP_Server_IsRunning() && VISAServer_IsRunning();
}

/** @brief          Set the thermal frame data for both HTTP and WebSocket endpoints.
 *  @param p_Frame  Pointer to thermal frame data
 */
static inline void Server_SetThermalFrame(ImageEncoder_Raw_t *p_Frame)
{
    HTTP_Server_SetRawFrame(p_Frame);
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

/** @brief          Set the Lepton temperatures for both HTTP and WebSocket endpoints.
 *  @param FPA      Focal Plane Array temperature in Degree Celsius
 *  @param AUX      Auxiliary temperature in Degree Celsius
 */
static inline void Server_SetLeptonTemperatures(float FPA, float Aux)
{
    HTTP_Server_SetLeptonTemperatures(FPA, Aux);
}

/** @brief              Set the device temperature for both HTTP and WebSocket endpoints.
 *  @param Temperature  Device temperature in Degree Celsius
 */
static inline void Server_SetDeviceTemperature(float Temperature)
{
    HTTP_Server_SetDeviceTemperature(Temperature);
}

#endif /* SERVER_H_ */
