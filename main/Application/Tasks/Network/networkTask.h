/*
 * networkTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network task definition.
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

#ifndef NETWORK_TASK_H_
#define NETWORK_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/application.h"
#include "Application/Manager/Network/networkTypes.h"

/** @brief              Initialize the network task.
 *                      Creates FreeRTOS task for network operations including WiFi
 *                      management, HTTP server, WebSocket, and VISA server.
 *  @note               Call this after NetworkManager_Init().
 *                      Task handles WiFi events and client connections.
 *  @param p_AppContext Pointer to the application context
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_AppContext is NULL
 *                      ESP_ERR_NO_MEM if task creation fails
 *                      ESP_FAIL if network initialization fails
 */
esp_err_t Network_Task_Init(App_Context_t *p_AppContext);

/** @brief          Deinitialize the network task.
 *                  Stops the task and frees all resources. Network connections are
 *                  terminated.
 *  @note           Task must be stopped before calling this.
 *                  All client connections are closed.
 */
void Network_Task_Deinit(void);

/** @brief          Start the network task.
 *                  Resumes the task to handle network operations and event processing.
 *  @note           Task processes WiFi events and manages connections.
 *                  Servers start when WiFi connection is established.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 *                  ESP_FAIL if task start fails
 */
esp_err_t Network_Task_Start(void);

/** @brief          Stop the network task.
 *                  Suspends network task execution. Existing connections remain active
 *                  but no new connections are accepted.
 *  @note           WiFi remains connected.
 *                  To disconnect WiFi, use NetworkManager_DisconnectWiFi().
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not running
 *                  ESP_FAIL if stop operation fails
 */
esp_err_t Network_Task_Stop(void);

/** @brief          Check if the network task is running.
 *                  Returns the current task execution status.
 *  @note           Thread-safe.
 *  @return         true if task is running and processing events
 *                  false if task is stopped or not initialized
 */
bool Network_Task_IsRunning(void);

#endif /* NETWORK_TASK_H_ */