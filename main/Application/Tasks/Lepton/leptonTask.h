/*
 * leptonTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Lepton camera task definition.
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

#ifndef LEPTON_TASK_H_
#define LEPTON_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/app_types.h"

/** @brief          Initialize the Lepton camera task.
 *                  Creates FreeRTOS task for Lepton thermal camera frame acquisition.
 *                  Sets up queues, event handlers, and camera interface.
 *  @note           Task priority and stack size defined in task header.
 *                  Call this before Lepton_Task_Start().
 *                  Requires Lepton camera connected via SPI.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if task or queue creation fails
 *                  ESP_FAIL if Lepton initialization fails
 */
esp_err_t Lepton_Task_Init(void);

/** @brief          Deinitialize the Lepton camera task.
 *                  Stops the task, deletes queues, and frees all resources.
 *                  Camera is left in its current state.
 *  @note           Task must be stopped before calling this.
 *  @warning        All frame data and queues are lost.
 */
void Lepton_Task_Deinit(void);

/** @brief              Start the Lepton camera task.
 *                      Resumes the FreeRTOS task to begin frame acquisition from the
 *                      Lepton thermal camera. Frames are posted as events.
 *  @note               Frame rate depends on Lepton model (9 Hz typical).
 *                      Posts APP_EVENT_LEPTON_FRAME_READY with frame data.
 *  @param p_AppContext Pointer to the application context
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_AppContext is NULL
 *                      ESP_ERR_INVALID_STATE if not initialized
 *                      ESP_FAIL if camera start fails
 */
esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext);

/** @brief          Stop the Lepton camera task.
 *                  Suspends frame acquisition. Camera remains initialized and can
 *                  be restarted with Lepton_Task_Start().
 *  @note           Frame events stop being posted.
 *                  Camera power remains on (low power mode).
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not running
 *                  ESP_FAIL if stop operation fails
 */
esp_err_t Lepton_Task_Stop(void);

/** @brief          Check if the Lepton task is currently running.
 *                  Returns the current task execution status.
 *  @note           Thread-safe.
 *  @return         true if task is running and acquiring frames
 *                  false if task is stopped or not initialized
 */
bool Lepton_Task_IsRunning(void);

#endif /* LEPTON_TASK_H_ */