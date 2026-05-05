/*
 * guiROI.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: ROI functions for the GUI task.
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

#ifndef GUI_ROI_H_
#define GUI_ROI_H_

#include "../Export/ui.h"

#include "guiHelper.h"

/** @brief          Initialize the ROI configuration mode. */
void GUI_ROI_Init(void);

/** @brief          Deinitialize the ROI configuration mode. */
void GUI_ROI_Deinit(void);

/** @brief          Switch to the specified ROI.
 *  @param ROI      ROI index
 */
void GUI_ROI_SwitchToROI(uint8_t ROI);

/** @brief          Save all ROIs to the memory. */
void GUI_ROI_Save(void);

/** @brief          Load the ROIs from the memory and draw them on the display.
 */
void GUI_ROI_Load(void);

/** @brief          Change the specified ROI by the given delta values and update its position and size on the display.
 *  @param DeltaX   Change in X position
 *  @param DeltaY   Change in Y position
 *  @param DeltaW   Change in width
 *  @param DeltaH   Change in height
 */
void GUI_ROI_Change(int16_t DeltaX, int16_t DeltaY, int16_t DeltaW, int16_t DeltaH);

/** @brief          Reset the currently selected ROI to its default position and size.
 */
void GUI_ROI_Reset(void);

/** @brief          Reset all ROIs to their default positions and sizes.
 */
void GUI_ROI_ResetAll(void);

#endif /* GUI_ROI_H_ */