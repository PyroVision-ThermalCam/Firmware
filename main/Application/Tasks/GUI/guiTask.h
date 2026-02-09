/*
 * guiTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task definition.
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

#ifndef GUI_TASK_H_
#define GUI_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/application.h"

ESP_EVENT_DECLARE_BASE(GUI_EVENTS);

/** @brief  Initialize the GUI task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t GUI_Task_Init(void);

/** @brief Deinitialize the GUI task.
 */
void GUI_Task_Deinit(void);

/** @brief              Start the GUI task.
 *  @param p_AppContext Pointer to the application context
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t GUI_Task_Start(App_Context_t *p_AppContext);

/** @brief  Stop the GUI task.
 *  @return ESP_OK on success, error code otherwise
 */
esp_err_t GUI_Task_Stop(void);

/** @brief  Check if the GUI task is running.
 *  @return true if running, false otherwise
 */
bool GUI_Task_IsRunning(void);

/** @brief Toggle ROI (Region of Interest) edit mode.
 *         When enabled, shows a draggable rectangle overlay on the thermal image
 *         that can be moved by touch to adjust the spotmeter region.
 */
void GUI_Toggle_ROI_EditMode(void);

/** @brief          Save current thermal image to storage as BMP file.
 *  @note           Checks if filesystem is locked (USB active) before writing.
 *                  Creates filename with timestamp: THERMAL_YYYYMMDD_HHMMSS.BMP
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if filesystem is locked (USB active)
 *                  ESP_ERR_NO_MEM if no frame available
 *                  ESP_FAIL if file write failed
 */
esp_err_t GUI_SaveThermalImage(void);

#endif /* GUI_TASK_H_ */