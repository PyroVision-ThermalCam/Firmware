/*
 * guiCrosshair.cpp
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

#include <esp_event.h>

#include "guiHelper.h"
#include "guiCrosshair.h"
#include "guiImageSave.h"
#include "../guiTask.h"
#include "../guiControl.h"

/** @brief Nominal y offset: label centre is this many pixels above/below the crosshair centre.
 */
#define LABEL_Y_OFFSET      15

/** @brief Hysteresis: crosshair must move this far past the flip threshold before reverting.
 */
#define HYSTERESIS          5

/** @brief Fallback label height before LVGL has performed the first layout pass.
 */
#define LBL_H_FALLBACK      15

extern GUI_Task_State_t _GUITaskState;

/** @brief          Reposition ui_Label_Main_Thermal_PixelTemperature relative to the crosshair
 *                  centre so the label text always stays within the visible thermal canvas.
 *                  The label is given a fixed width equal to the container width so that its
 *                  bounding box is always confined to [0, CROSSHAIR_CONTAINER_W] within the
 *                  container — regardless of the actual text length.  Text visibility near
 *                  the image edges is achieved by changing the text-align style rather than
 *                  by shifting the label's x position:
 *                    - Default:         LV_TEXT_ALIGN_CENTER, dy = –LABEL_Y_OFFSET (above).
 *                    - Near top edge:   dy = +LABEL_Y_OFFSET (below); text-align unchanged.
 *                    - Near left edge:  LV_TEXT_ALIGN_RIGHT (text hugs the right/visible side).
 *                    - Near right edge: LV_TEXT_ALIGN_LEFT  (text hugs the left/visible side).
 *  @note           Because the label bounding box never leaves the container, no dx shift or
 *                  clamp arithmetic is needed and clipping artefacts are impossible.
 *                  Must be called from the GUI task only (not thread-safe).
 *                  Flip state is stored in static locals; hysteresis prevents flickering at
 *                  the threshold positions.
 */
static void GUI_Crosshair_UpdateTempLabelPosition(void)
{
    int32_t dy;
    int32_t LabelH;
    int32_t TopWhenAbove;
    lv_text_align_t TextAlign;

    /* Give the label a fixed pixel width equal to the container width once.
     * With a fixed-width label centred at dx=0, the label spans [0, CROSSHAIR_CONTAINER_W]
     * within the container — it is always inside the container's clip region and can never
     * be partially cut off.  All horizontal "positioning" is handled by text-align alone. */
    if (_GUITaskState.Crosshair.Initialized == false) {
        lv_obj_set_width(ui_Label_Main_Thermal_Pixel_Temperature, GUI_CROSSHAIR_CONTAINER_W);
        _GUITaskState.Crosshair.Initialized = true;
    }

    LabelH = lv_obj_get_height(ui_Label_Main_Thermal_Pixel_Temperature);
    if (LabelH <= 0) {
        LabelH = LBL_H_FALLBACK;
    }

    /* Vertical flip (above <-> below crosshair). */
    TopWhenAbove = _GUITaskState.Crosshair.Y - LABEL_Y_OFFSET - (LabelH / 2);

    if (_GUITaskState.Crosshair.FlippedY == false) {
        if (TopWhenAbove < 0) {
            _GUITaskState.Crosshair.FlippedY = true;
        }
    } else {
        if (TopWhenAbove >= HYSTERESIS) {
            _GUITaskState.Crosshair.FlippedY = false;
        }
    }

    dy = _GUITaskState.Crosshair.FlippedY ? LABEL_Y_OFFSET : -LABEL_Y_OFFSET;

    /* Horizontal flip via text-align.
     * Flip threshold = HalfContainerW: when the container's left/right edge crosses the
     * image boundary the text would otherwise be invisible — anchoring it to the near
     * (visible) side of the label keeps all digits on screen.
     *
     * Near left edge  (CX < HalfContainerW):
     *   Container left goes outside the image.  Anchor text to the right of the label
     *   (= the portion still inside the image) with LV_TEXT_ALIGN_RIGHT.
     *
     * Near right edge (CX > ImageW - HalfContainerW):
     *   Container right goes outside the image.  Anchor text to the left with
     *   LV_TEXT_ALIGN_LEFT. */
    if (_GUITaskState.Crosshair.FlippedLeft == false) {
        if (_GUITaskState.Crosshair.X < (GUI_CROSSHAIR_CONTAINER_W / 2)) {
            _GUITaskState.Crosshair.FlippedLeft = true;
        }
    } else {
        if (_GUITaskState.Crosshair.X >= ((GUI_CROSSHAIR_CONTAINER_W / 2) + HYSTERESIS)) {
            _GUITaskState.Crosshair.FlippedLeft = false;
        }
    }

    if (_GUITaskState.Crosshair.FlippedRight == false) {
        if (_GUITaskState.Crosshair.X > (static_cast<int32_t>(GUI_IMAGE_CANVAS_WIDTH) - (GUI_CROSSHAIR_CONTAINER_W / 2))) {
            _GUITaskState.Crosshair.FlippedRight = true;
        }
    } else {
        if (_GUITaskState.Crosshair.X <= (static_cast<int32_t>(GUI_IMAGE_CANVAS_WIDTH) - (GUI_CROSSHAIR_CONTAINER_W / 2) - HYSTERESIS)) {
            _GUITaskState.Crosshair.FlippedRight = false;
        }
    }

    if (_GUITaskState.Crosshair.FlippedLeft == true) {
        TextAlign = LV_TEXT_ALIGN_RIGHT;
    } else if (_GUITaskState.Crosshair.FlippedRight == true) {
        TextAlign = LV_TEXT_ALIGN_LEFT;
    } else {
        TextAlign = LV_TEXT_ALIGN_CENTER;
    }

    lv_obj_set_style_text_align(ui_Label_Main_Thermal_Pixel_Temperature, TextAlign, LV_PART_MAIN);
    lv_obj_set_x(ui_Label_Main_Thermal_Pixel_Temperature, 0);
    lv_obj_set_y(ui_Label_Main_Thermal_Pixel_Temperature, dy);
}

void GUI_Crosshair_Init(void)
{
    _GUITaskState.Crosshair.Initialized = false;    /**< true once the label width has been set to a fixed value. */
    _GUITaskState.Crosshair.FlippedY = false;       /**< true while label is shown below the crosshair. */
    _GUITaskState.Crosshair.FlippedLeft = false;    /**< true while text is right-aligned (near left image edge). */
    _GUITaskState.Crosshair.FlippedRight = false;   /**< true while text is left-aligned (near right image edge). */

    /* CrosshairX/Y are centre coordinates. The container top-left is placed at
     * (Cx - W/2, Cy - H/2).  Allow the centre to reach every pixel of the canvas;
     * LVGL clips the container automatically when it extends past the canvas edge,
     * so the crosshair marker remains visible even at the very edge. */
    _GUITaskState.Crosshair.MinX = 0;
    _GUITaskState.Crosshair.MaxX = static_cast<int32_t>(GUI_IMAGE_CANVAS_WIDTH)  - 1;;
    _GUITaskState.Crosshair.MinY = 0;
    _GUITaskState.Crosshair.MaxY = static_cast<int32_t>(GUI_IMAGE_CANVAS_HEIGHT) - 1;;

    /* Show the crosshair at boot, centred on the canvas.
     * CrosshairX/Y store the centre pixel of the crosshair marker (not the container's
     * top-left corner).  lv_obj_set_pos() is called with (CX - W/2, CY - H/2) so that
     * the marker sits exactly at the stored position; negative values are valid and allow
     * the crosshair to reach every edge of the image.
     * LV_ALIGN_TOP_LEFT is required so that LVGL treats the position as an absolute offset
     * from the parent's top-left origin; without it negative offsets are clamped to 0.
     */
    _GUITaskState.Crosshair.Show = true;
    _GUITaskState.Crosshair.X = static_cast<int32_t>(GUI_IMAGE_CANVAS_WIDTH)  / 2;
    _GUITaskState.Crosshair.Y = static_cast<int32_t>(GUI_IMAGE_CANVAS_HEIGHT) / 2;
    lv_obj_set_align(ui_Container_Main_Thermal_Crosshair, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_Container_Main_Thermal_Crosshair,
                   _GUITaskState.Crosshair.X - GUI_CROSSHAIR_CONTAINER_W / 2,
                   _GUITaskState.Crosshair.Y - GUI_CROSSHAIR_CONTAINER_H / 2);
    lv_obj_remove_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);

    GUI_Crosshair_UpdateTempLabelPosition();
}

void GUI_Crosshair_Set(void)
{
    /* Skip, if the crosshair is not shown or has not moved. */
    if ((_GUITaskState.Crosshair.Show == false) || (_GUITaskState.Crosshair.LastX == _GUITaskState.Crosshair.X ) || (_GUITaskState.Crosshair.LastY == _GUITaskState.Crosshair.Y)) {
        return;
    }

    if (_GUITaskState.Crosshair.X < _GUITaskState.Crosshair.MinX) {
        _GUITaskState.Crosshair.X = _GUITaskState.Crosshair.MinX;
    }

    if (_GUITaskState.Crosshair.X > _GUITaskState.Crosshair.MaxX) {
        _GUITaskState.Crosshair.X = _GUITaskState.Crosshair.MaxX;
    }

    if (_GUITaskState.Crosshair.Y < _GUITaskState.Crosshair.MinY) {
        _GUITaskState.Crosshair.Y = _GUITaskState.Crosshair.MinY;
    }

    if (_GUITaskState.Crosshair.Y > _GUITaskState.Crosshair.MaxY) {
        _GUITaskState.Crosshair.Y = _GUITaskState.Crosshair.MaxY;
    }

    _GUITaskState.Crosshair.LastX = _GUITaskState.Crosshair.X - (GUI_CROSSHAIR_CONTAINER_W / 2);
    _GUITaskState.Crosshair.LastY = _GUITaskState.Crosshair.Y - (GUI_CROSSHAIR_CONTAINER_H / 2);

    lv_obj_set_pos(ui_Container_Main_Thermal_Crosshair,
                   _GUITaskState.Crosshair.LastX,
                   _GUITaskState.Crosshair.LastY);

    GUI_Crosshair_UpdateTempLabelPosition();
}

void GUI_Crosshair_IncreaseY(void)
{
    _GUITaskState.Crosshair.Y += static_cast<int32_t>(GUI_CROSSHAIR_STEP_PX);
}

void GUI_Crosshair_DecreaseY(void)
{
    _GUITaskState.Crosshair.Y -= static_cast<int32_t>(GUI_CROSSHAIR_STEP_PX);
}

void GUI_Crosshair_IncreaseX(void)
{
    _GUITaskState.Crosshair.X += static_cast<int32_t>(GUI_CROSSHAIR_STEP_PX);
}

void GUI_Crosshair_DecreaseX(void)
{
    _GUITaskState.Crosshair.X -= static_cast<int32_t>(GUI_CROSSHAIR_STEP_PX);
}

void GUI_Crosshair_Show(void)
{
    if (_GUITaskState.Crosshair.Show) {
        lv_obj_remove_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
    }
}

void GUI_Crosshair_Hide(void)
{
    _GUITaskState.Crosshair.Show = false;
    lv_obj_add_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
}