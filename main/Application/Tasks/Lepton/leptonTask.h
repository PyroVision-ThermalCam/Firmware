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

#include "Application/application.h"

/** @brief  Initialize the Lepton task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Lepton_Task_Init(void);

/** @brief Deinitialize the Lepton task.
 */
void Lepton_Task_Deinit(void);

/** @brief              Start the Lepton task.
 *  @param p_AppContext Pointer to the application context
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext);

/** @brief  Stop the Lepton task
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t Lepton_Task_Stop(void);

/** @brief  Check if the Lepton task is currently running
 *  @return true if running, false otherwise
 */
bool Lepton_Task_IsRunning(void);

#endif /* LEPTON_TASK_H_ */