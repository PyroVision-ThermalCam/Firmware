/*
 * guiImageSave.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task image save implementation.
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

#ifndef GUI_IMAGE_SAVE_H_
#define GUI_IMAGE_SAVE_H_

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "guiHelper.h"

/** @brief          Image save task. Runs in background to avoid blocking GUI.
 *  @param p_Param  Task parameters (unused)
 */
void Task_ImageSave(void *p_Param);

#endif /* GUI_IMAGE_SAVE_H_ */
