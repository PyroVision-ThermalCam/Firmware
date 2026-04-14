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

#include <freertos/FreeRTOS.h>

#include <stdint.h>

#include "Application/application.h"

/** @brief          Initialize the devices task.
 *                  Creates the FreeRTOS task, event group, and registers event handlers
 *                  for settings and time-synchronisation events.
 *  @note           Call this before Devices_Task_Start().
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if event group or task creation fails
 *                  ESP_FAIL if DevicesManager_Init() fails
 */
esp_err_t Devices_Task_Init(void);

/** @brief  Deinitialize the devices task.
 *          Stops the task, deletes the event group, and unregisters all event handlers.
 *  @note   Task must be stopped before calling this.
 */
void Devices_Task_Deinit(void);

/** @brief              Start the devices task.
 *                      Resumes the FreeRTOS task to begin processing device events and
 *                      periodic I2C sensor reads.
 *  @note               Call this after Devices_Task_Init().
 *  @param p_AppContext Pointer to the application context.
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_AppContext is NULL
 *                      ESP_ERR_INVALID_STATE if not initialized
 *                      ESP_ERR_NO_MEM if task creation fails
 */
esp_err_t Devices_Task_Start(App_Context_t *p_AppContext);

/** @brief          Stop the devices task.
 *                  Suspends device event processing. Peripheral hardware remains
 *                  powered and can be resumed with Devices_Task_Start().
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not running
 */
esp_err_t Devices_Task_Stop(void);

/** @brief          Check if the devices task is running.
 *  @note           Thread-safe.
 *  @return         true  if the task is executing
 *                  false if the task is stopped or not initialized
 */
bool Devices_Task_IsRunning(void);

#endif /* DEVICES_TASK_H_ */