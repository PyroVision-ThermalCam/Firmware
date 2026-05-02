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

#include <lvgl.h>

#include <stdint.h>

#include "guiControl.h"
#include "Application/application.h"

/** @brief              Initialize the GUI task.
 *                      Allocates display and canvas buffers, creates the image-save queue and
 *                      background task, sets up the LVGL display and touch/keypad input devices.
 *  @note               Call this before GUI_Task_Start().
 *                      Requires DevicesManager (I2C bus) to be initialized first.
 *  @return             ESP_OK on success
 *                      ESP_ERR_NO_MEM if buffer allocation or task/queue creation fails
 *                      ESP_FAIL if LVGL or LCD initialization fails
 */
esp_err_t GUI_Task_Init(void);

/** @brief              Deinitialize the GUI task.
 *                      Stops the task, frees all LVGL resources, and releases display/canvas buffers.
 *  @note               Task must be stopped before calling this.
 */
void GUI_Task_Deinit(void);

/** @brief              Start the GUI task.
 *                      Resumes the FreeRTOS task to begin rendering the LVGL UI and
 *                      processing touch/keypad and sensor-data events.
 *  @note               Call this after GUI_Task_Init().
 *  @param p_AppContext Pointer to the application context.
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_AppContext is NULL
 *                      ESP_ERR_INVALID_STATE if not initialized
 *                      ESP_ERR_NO_MEM if task creation fails
 */
esp_err_t GUI_Task_Start(App_Context_t *p_AppContext);

/** @brief          Stop the GUI task.
 *                  Suspends UI rendering. LVGL remains initialized and the display
 *                  retains its last content.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not running
 */
esp_err_t GUI_Task_Stop(void);

/** @brief          Check if the GUI task is running.
 *  @note           Thread-safe.
 *  @return         true  if the task is executing and rendering the UI
 *                  false if the task is stopped or not initialized
 */
bool GUI_Task_IsRunning(void);

/** @brief          Activate the ROI (Region of Interest) configuration screen.
 */
void GUI_Task_ActivateROIConfig(void);

/** @brief          Return the LVGL keypad input device handle.
 *  @note           Required to bind an lv_group_t to the physical keypad.
 *                  Returns NULL if the GUI has not been initialised yet.
 *  @return         lv_indev_t* keypad handle, or NULL
 */
lv_indev_t *GUI_Task_GetKeypadIndev(void);

#endif /* GUI_TASK_H_ */