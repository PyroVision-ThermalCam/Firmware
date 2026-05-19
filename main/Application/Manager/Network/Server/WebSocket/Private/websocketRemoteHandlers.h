/*
 * websocketRemoteHandlers.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WebSocket handlers for remote control interface.
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

#ifndef WEBSOCKET_REMOTE_HANDLERS_H_
#define WEBSOCKET_REMOTE_HANDLERS_H_

#include <esp_err.h>

#include <stdint.h>

#include <cJSON.h>

/** @brief          Send JSON message to WebSocket client.
 *  @param FD       File descriptor of client socket
 *  @param p_Cmd    Command string
 *  @param p_Data   Optional JSON data object (can be NULL)
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if JSON creation failed
 *                  ESP_ERR_INVALID_STATE if send failed
 */
esp_err_t WebSocket_SendJSON(int FD, const char *p_Cmd, cJSON *p_Data);

/** @brief              Handle "get_temperature" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetTemperature(int FD, cJSON *p_Data);

/** @brief              Handle "get_time" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetTime(int FD, cJSON *p_Data);

/** @brief              Handle "set_time" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "time" field)
 */
void WebSocket_Handle_SetTime(int FD, cJSON *p_Data);

/** @brief              Handle "get_battery" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetBattery(int FD, cJSON *p_Data);

/** @brief              Handle "get_lepton_emissivity" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetLeptonEmissivity(int FD, cJSON *p_Data);

/** @brief              Handle "set_lepton_emissivity" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "emissivity" field)
 */
void WebSocket_Handle_SetLeptonEmissivity(int FD, cJSON *p_Data);

/** @brief              Handle "get_lepton_stats" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetLeptonStats(int FD, cJSON *p_Data);

/** @brief              Handle "get_lepton_roi" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetLeptonROI(int FD, cJSON *p_Data);

/** @brief              Handle "set_lepton_roi" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain ROI fields)
 */
void WebSocket_Handle_SetLeptonROI(int FD, cJSON *p_Data);

/** @brief              Handle "get_lepton_spotmeter" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetLeptonSpotmeter(int FD, cJSON *p_Data);

/** @brief              Handle "get_flash" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetFlash(int FD, cJSON *p_Data);

/** @brief              Handle "set_flash" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "enabled" and/or "power" fields)
 */
void WebSocket_Handle_SetFlash(int FD, cJSON *p_Data);

/** @brief              Handle "get_image_format" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetImageFormat(int FD, cJSON *p_Data);

/** @brief              Handle "set_image_format" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "format" field)
 */
void WebSocket_Handle_SetImageFormat(int FD, cJSON *p_Data);

/** @brief              Handle "set_status_led" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "color" and "brightness" fields)
 */
void WebSocket_Handle_SetStatusLED(int FD, cJSON *p_Data);

/** @brief              Handle "get_sd_state" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetSDState(int FD, cJSON *p_Data);

/** @brief              Handle "format_memory" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_FormatMemory(int FD, cJSON *p_Data);

/** @brief              Handle "display_message" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "message" field)
 */
void WebSocket_Handle_DisplayMessage(int FD, cJSON *p_Data);

/** @brief              Handle "get_lock" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (unused)
 */
void WebSocket_Handle_GetLock(int FD, cJSON *p_Data);

/** @brief              Handle "set_lock" command.
 *  @param FD           Client file descriptor
 *  @param p_Data       Command data (must contain "locked" field)
 */
void WebSocket_Handle_SetLock(int FD, cJSON *p_Data);

#endif /* WEBSOCKET_REMOTE_HANDLERS_H_ */
