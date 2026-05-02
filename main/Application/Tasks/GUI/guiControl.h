/*
 * guiControl.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Control functions for the GUI task.
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

#ifndef GUI_CONTROL_H_
#define GUI_CONTROL_H_

#include <stdint.h>

/** @brief          Handle Button1 click event.
 *                  The action depends on the current screen.
 */
void GUI_Control_HandleButton1(void);

/** @brief          Handle Button2 click event.
 *                  The action depends on the current screen.
 */
void GUI_Control_HandleButton2Short(void);

/** @brief          Handle Button3 click event.
 *                  The action depends on the current screen.
 */
void GUI_Control_HandleButton3(void);

/** @brief          Handle Button4 click event.
 *                  The action depends on the current screen.
 */
void GUI_Control_HandleButton4(void);

/** @brief          Handle joystick events.
 *                  The action depends on the current screen and the direction of the joystick event.
 *  @param Key      The key code of the joystick event (GUI_KEYPAD_JOY_UP/DOWN/LEFT/RIGHT/CENTER).
 */
void GUI_Control_HandleJoystick(uint32_t Key);

#endif /* GUI_CONTROL_H_ */