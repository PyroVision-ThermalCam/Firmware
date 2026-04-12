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

#include "Application/application.h"

ESP_EVENT_DECLARE_BASE(CAMERA_EVENTS);

/** @brief Camera task event identifiers posted to the default event loop.
 */
enum {
    CAMERA_EVENT_INIT_COMPLETE, /**< Camera hardware (and AF, if supported) initialised successfully. No event data. */
    CAMERA_EVENT_INIT_FAILED,   /**< Camera hardware initialisation failed. Event data: esp_err_t (4 bytes). */
};

/** @brief  Initializes the camera task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Camera_Task_Init(void);

/** @brief Deinitializes the camera task.
 */
void Camera_Task_Deinit(void);

/** @brief  Starts the camera task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Camera_Task_Start(App_Context_t *p_AppContext);

/** @brief  Stops the camera task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Camera_Task_Stop(void);

/** @brief  Checks if the camera task is running.
 *  @return false if the task is not running, true if it is running
 */
bool Camera_Task_IsRunning(void);

#endif /* CAMERA_TASK_H_ */