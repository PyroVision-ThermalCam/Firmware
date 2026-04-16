/*
 * ui_messagebox.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings UI implementation.
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

#ifndef UI_MESSAGEBOX_H_
#define UI_MESSAGEBOX_H_

#include <lvgl.h>

#include <stdint.h>

/** @brief                  Show a message box with the specified title and auto-close it after a delay.
 *  @param p_Title          Title to show in the message box
 *  @param AutoCloseDelay   Time in seconds after which the message box should auto-close (default: 1 second)
 */
void MessageBox_Show(const char *p_Title, uint32_t AutoCloseDelay = 1);

/** @brief          Show a persistent "in progress" message box without auto-close.
 *                  The box must be dismissed by calling MessageBox_CloseProgress().
 *  @param p_Title  Title text to display inside the box
 */
void MessageBox_ShowProgress(const char *p_Title);

/** @brief  Close the persistent progress message box opened by MessageBox_ShowProgress().
 *          Has no effect if no progress box is currently open.
 */
void MessageBox_CloseProgress(void);

/** @brief          Show a message box indicating the result of a thermal image save operation.
 *  @param Error    ESP_OK if the image was saved successfully, or an error code if it failed
 */
void MessageBox_ImageSaveError(esp_err_t Error);

#endif /* UI_MESSAGEBOX_H_ */