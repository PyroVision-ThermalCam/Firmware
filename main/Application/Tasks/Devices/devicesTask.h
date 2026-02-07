/*
 * devicesTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Devices task definition.
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

#ifndef DEVICES_TASK_H_
#define DEVICES_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/application.h"

/** @brief  Initializes the devices task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Devices_Task_Init(void);

/** @brief Deinitializes the devices task.
 */
void Devices_Task_Deinit(void);

/** @brief  Starts the devices task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Devices_Task_Start(App_Context_t *p_AppContext);

/** @brief  Stops the devices task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Devices_Task_Stop(void);

/** @brief  Checks if the devices task is running.
 *  @return false if the task is not running, true if it is running
 */
bool Devices_Task_IsRunning(void);

#endif /* DEVICES_TASK_H_ */