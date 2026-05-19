/*
 * cameraTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Camera task definition.
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

#ifndef CAMERA_TASK_H_
#define CAMERA_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/app_types.h"

ESP_EVENT_DECLARE_BASE(CAMERA_TASK_EVENTS);

/** @brief              Initialize the camera task.
 *                      Creates the FreeRTOS event group and prepares internal state.
 *  @note               Call this before Camera_Task_Start().
 *  @return             ESP_OK on success
 *                      ESP_ERR_NO_MEM if event group creation fails
 */
esp_err_t Camera_Task_Init(void);

/** @brief              Deinitialize the camera task.
 *                      Stops the task, deletes the event group, and releases all resources.
 *  @note               Task must be stopped before calling this.
 */
void Camera_Task_Deinit(void);

/** @brief              Start the camera task.
 *  @note               Call this after Camera_Task_Init().
 *  @param p_AppContext Pointer to the application context.
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_AppContext is NULL
 *                      ESP_ERR_INVALID_STATE if not initialized
 *                      ESP_ERR_NO_MEM if task creation fails
 */
esp_err_t Camera_Task_Start(App_Context_t *p_AppContext);

/** @brief              Stop the camera task.
 *                      Suspends camera capture. The sensor remains powered.
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_STATE if not running
 */
esp_err_t Camera_Task_Stop(void);

/** @brief              Check if the camera task is running.
 *  @note               Thread-safe.
 *  @return             true  if the task is executing
 *                      false if the task is stopped or not initialized
 */
bool Camera_Task_IsRunning(void);

#endif /* CAMERA_TASK_H_ */