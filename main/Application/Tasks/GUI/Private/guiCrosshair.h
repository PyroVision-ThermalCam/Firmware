/*
 * guiCrosshair.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Crosshair functions for the GUI task.
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

#ifndef GUI_CROSSHAIR_H_
#define GUI_CROSSHAIR_H_

#include "../Export/ui.h"

#include "guiHelper.h"

#define GUI_CROSSHAIR_STEP_PX                       8
#define GUI_CROSSHAIR_INITIAL_DELAY_MS              30
#define GUI_CROSSHAIR_AUTOREPEAT_DELAY_MS           400
#define GUI_CROSSHAIR_AUTOREPEAT_PERIOD_MS          150

/** @brief  Fixed container dimensions taken from the UI export (ui_Main.c).
 *          Using constants avoids calling lv_obj_get_width/height on a potentially
 *          hidden widget whose coords may not be up-to-date at indev-callback time.
 */
#define GUI_CROSSHAIR_CONTAINER_W                   100
#define GUI_CROSSHAIR_CONTAINER_H                   50

/** @brief          Initialize the Crosshair configuration mode. */
void GUI_Crosshair_Init(void);

void GUI_Crosshair_Set(void);

/** @brief          Move the crosshair by one step in the current direction.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_IncreaseY(void);

/** @brief          Move the crosshair by one step in the current direction.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_DecreaseY(void);

/** @brief          Move the crosshair by one step in the current direction.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_IncreaseX(void);

/** @brief          Move the crosshair by one step in the current direction.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_DecreaseX(void);

/** @brief          Show the crosshair and its child labels.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_Show(void);

/** @brief          Hide the crosshair and its child labels.  Must be called from the GUI task only (not thread-safe).
 */
void GUI_Crosshair_Hide(void);

#endif /* GUI_CROSSHAIR_H_ */