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

#include "Application/application.h"

/** @brief Custom key codes for Button2–4.
 *         Values are outside the LV_KEY_* range so LVGL does not consume them
 *         for group navigation — they are forwarded as LV_EVENT_KEY to the
 *         current group's focused object where per-screen handlers pick them up.
 */
#define GUI_KEYPAD_BTN1   ((uint32_t)0x0101U)   /**< Button1: Main Menu / Menu Back / Info Back */
#define GUI_KEYPAD_BTN2   ((uint32_t)0x0102U)   /**< Button2: Main ROI */
#define GUI_KEYPAD_BTN3   ((uint32_t)0x0103U)   /**< Button3: Main Info */
#define GUI_KEYPAD_BTN4   ((uint32_t)0x0104U)   /**< Button4: Main/Menu Save */

/** @brief          Initialize the GUI task.
 *                  Allocates display and canvas buffers, creates the image-save queue and
 *                  background task, sets up the LVGL display and touch/keypad input devices.
 *  @note           Call this before GUI_Task_Start().
 *                  Requires DevicesManager (I2C bus) to be initialized first.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if buffer allocation or task/queue creation fails
 *                  ESP_FAIL if LVGL or LCD initialization fails
 */
esp_err_t GUI_Task_Init(void);

/** @brief  Deinitialize the GUI task.
 *          Stops the task, frees all LVGL resources, and releases display/canvas buffers.
 *  @note   Task must be stopped before calling this.
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

/** @brief Toggle ROI (Region of Interest) edit mode.
 *         When enabled, shows a draggable rectangle overlay on the thermal image
 *         that can be moved by touch to adjust the spotmeter region.
 */
void GUI_Toggle_ROI_EditMode(void);

/** @brief          Request to save the next thermal image to storage as PNG file.
 *  @note           Sets a flag that triggers image save on the next frame update.
 *                  The actual save happens in background task (non-blocking).
 *                  A message box will be displayed upon completion or error.
 *                  Saves the scaled 240x180 display image (not the raw 160x120 frame).
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if filesystem is locked (USB active)
 */
esp_err_t GUI_SaveThermalImage(void);

/** @brief          Enable or disable the visible-light camera view in the thermal canvas.
 *                  When enabled, incoming camera frames are scaled from 320x240 to 240x180 using
 *                  bilinear interpolation and rendered in place of the thermal image. Temperature
 *                  overlay labels are hidden automatically. When disabled, the thermal view is
 *                  restored and the temperature labels are shown again.
 *  @note           Thread-safe. The change takes effect on the next task loop iteration.
 *  @param Enable   true to show the camera view, false to restore the thermal view.
 */
void GUI_Task_SetCameraView(bool Enable);

/** @brief          Toggle the visible-light camera view in the thermal canvas.
 *  @note           Thread-safe. The change takes effect on the next task loop iteration.
 */
void GUI_Task_ToggleCameraView(void);

/** @brief  Return the LVGL keypad input device handle.
 *  @note   Required to bind an lv_group_t to the physical keypad.
 *          Returns NULL if the GUI has not been initialised yet.
 *  @return lv_indev_t* keypad handle, or NULL
 */
lv_indev_t *GUI_Task_GetKeypadIndev(void);

#endif /* GUI_TASK_H_ */