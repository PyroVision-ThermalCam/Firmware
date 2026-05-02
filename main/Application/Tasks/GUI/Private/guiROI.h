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

/* Map ROI type to its LVGL overlay widget and apply the computed position/size.
 * NOTE: Stored as pointer-to-pointer so the current widget address (set by ui_init)
 *       is always resolved at call time, not at static-initialisation time (when
 *       all LVGL widget globals are still NULL). */
static lv_obj_t ** const ROI_Widgets[] = {
    &ui_Image_Main_Thermal_Spotmeter_ROI,
    &ui_Image_Main_Thermal_Scene_ROI,
    &ui_Image_Main_Thermal_AGC_ROI,
    &ui_Image_Main_Thermal_Video_Focus_ROI,
};

static const char *ROI_Labels[] = {
    "Spotmeter",
    "Scene",
    "AGC",
    "Video Focus"
};

/** @brief          Initialize the ROI configuration mode. */
void GUI_ROI_Init(void);

/** @brief          Deinitialize the ROI configuration mode. */
void GUI_ROI_Deinit(void);

/** @brief          Start the ROI fading animation.
 *  @param p_ROI    LVGL object representing the ROI
 */
void GUI_ROI_StartFadingAnimation(lv_obj_t *p_ROI);

/** @brief          Load the ROIs from the memory and draw them on the display.
 */
void GUI_ROI_Load(void);

/** @brief          Update the ROI rectangle on the GUI and on the Lepton.
 *  @param Type     ROI type
 *  @param x        X position of the ROI (in Lepton coordinates 0-159)
 *  @param y        Y position (in Lepton coordinates 0-119)
 *  @param w        Width of the ROI
 *  @param h        Height of the ROI
 */
static void GUI_ROI_Update(Settings_ROI_t ROI);

#endif /* GUI_ROI_H_ */