/*
 * guiROI.cpp
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

#include <esp_log.h>

#include "guiROI.h"

extern GUI_Task_State_t _GUITaskState;

static const char *TAG = "GUI-ROI";

/** @brief      Callback function for the ROI fading animation.
 *  @param obj  LVGL object being animated
 *  @param v    Current opacity value
 */
static void GUI_ROI_Fading_Callback(void * obj, int32_t v)
{
    lv_obj_set_style_opa(reinterpret_cast<lv_obj_t *>(obj), v, 0);
}

void GUI_ROI_Init(void)
{
    lv_obj_t *ROI_Widget;

    /* Enable ROI configuration mode */
    _GUITaskState.ROIConfig.IsActive = true;

    /* Always start with size change mode when ROI configuration mode is activated */
    _GUITaskState.ROIConfig.IsMovingActive = false;

    /* Hide all ROIs initially */
    for (int i = 0; i < (sizeof(ROI_Widgets) / sizeof(ROI_Widgets[0])); i++) {
        lv_obj_add_flag(*ROI_Widgets[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Always start with the first ROI when ROI configuration mode is activated */
    _GUITaskState.ROIConfig.SelectedROI = 0;

    /* Show the first ROI */
    ROI_Widget = *ROI_Widgets[_GUITaskState.ROIConfig.SelectedROI];
    lv_obj_remove_flag(ROI_Widget, LV_OBJ_FLAG_HIDDEN);

    GUI_ROI_StartFadingAnimation(ROI_Widget);

    /* Set the ROI name */
    lv_label_set_text(ui_Label_Main_ROI_Name, ROI_Labels[_GUITaskState.ROIConfig.SelectedROI]);

    /* Change the label of button 1 to "Back" */
    lv_label_set_text(ui_Label_Main_Button1, "\uF060");

    /* Change the label of button 2 to "Rotate" */
    lv_label_set_text(ui_Label_Main_Button2, "\uF2F1");

    /* Change the label of button 3 to "Resize" */
    lv_label_set_text(ui_Label_Main_Button3, "\uF065");

    /* Change the label of button 4 to "" */
}

void GUI_ROI_Deinit(void)
{
    /* Clear the ROI name */
    lv_label_set_text(ui_Label_Main_ROI_Name, "");

    /* Stop the current fading animation and restore full opacity */
    lv_anim_del(_GUITaskState.ROIConfig.FadingAnimation.var, GUI_ROI_Fading_Callback);
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(_GUITaskState.ROIConfig.FadingAnimation.var), LV_OPA_COVER, 0);

    _GUITaskState.ROIConfig.IsActive = false;
    lv_label_set_text(ui_Label_Main_Button1, "\uF0C9");
    lv_label_set_text(ui_Label_Main_Button2, "\uE595");
    lv_label_set_text(ui_Label_Main_Button3, "\uE0D8");
    lv_label_set_text(ui_Label_Main_Button4, LV_SYMBOL_SAVE);
}

void GUI_ROI_StartFadingAnimation(lv_obj_t *p_ROI)
{
    lv_anim_init(&_GUITaskState.ROIConfig.FadingAnimation);
    lv_anim_set_var(&_GUITaskState.ROIConfig.FadingAnimation, p_ROI);
    lv_anim_set_exec_cb(&_GUITaskState.ROIConfig.FadingAnimation, GUI_ROI_Fading_Callback);
    lv_anim_set_values(&_GUITaskState.ROIConfig.FadingAnimation, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&_GUITaskState.ROIConfig.FadingAnimation, 500);
    lv_anim_set_playback_time(&_GUITaskState.ROIConfig.FadingAnimation, 500);
    lv_anim_set_repeat_count(&_GUITaskState.ROIConfig.FadingAnimation, LV_ANIM_REPEAT_INFINITE);

    lv_anim_start(&_GUITaskState.ROIConfig.FadingAnimation);
}

void GUI_ROI_Load(void)
{
    int32_t DisplayWidth;
    int32_t DisplayHeight;
    Settings_Lepton_t LeptonSettings;

    DisplayWidth = lv_obj_get_width(ui_Image_Main_Image);
    DisplayHeight = lv_obj_get_height(ui_Image_Main_Image);

    /* Get the ROI settings from the NVS */
    SettingsManager_GetLepton(&LeptonSettings);

    for (size_t i = 0; i < (sizeof(LeptonSettings.ROI) / sizeof(LeptonSettings.ROI[0])); i++) {
        lv_obj_t *ROI_Widget = *ROI_Widgets[LeptonSettings.ROI[i].Type];

        if (ROI_Widget != NULL) {
            int32_t DispX = (LeptonSettings.ROI[i].x * DisplayWidth) / 160;
            int32_t DispY = (LeptonSettings.ROI[i].y * DisplayHeight) / 120;
            int32_t DispW = (LeptonSettings.ROI[i].w * DisplayWidth) / 160;
            int32_t DispH = (LeptonSettings.ROI[i].h * DisplayHeight) / 120;

            lv_obj_set_align(ROI_Widget, LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(ROI_Widget, DispX, DispY);
            lv_obj_set_size(ROI_Widget, DispW, DispH);
        }
    }
}

void GUI_ROI_Update(Settings_ROI_t ROI)
{
    int32_t DisplayWidth;
    int32_t DisplayHeight;
    Settings_Lepton_t SettingsLepton;

    if ((ROI.x + ROI.w) > 160) {
        ROI.w = 160 - ROI.x;
    }

    if ((ROI.y + ROI.h) > 120) {
        ROI.h = 120 - ROI.y;
    }

    /* Minimum size constraints */
    if (ROI.w < 1) {
        ROI.w = 1;
    } else if (ROI.h < 1) {
        ROI.h = 1;
    }

    /* Update visual rectangle on display (convert Lepton coords to display coords). */
    ESP_LOGD(TAG, "Updating ROI rectangle - Start: (%ld,%ld), End: (%ld,%ld), Size: %ldx%ld",
             ROI.x, ROI.y, ROI.x + ROI.w, ROI.y + ROI.h, ROI.w, ROI.h);

    DisplayWidth = lv_obj_get_width(ui_Image_Main_Image);
    DisplayHeight = lv_obj_get_height(ui_Image_Main_Image);

    int32_t DispX = (ROI.x * DisplayWidth) / 160;
    int32_t DispY = (ROI.y * DisplayHeight) / 120;
    int32_t DispW = (ROI.w * DisplayWidth) / 160;
    int32_t DispH = (ROI.h * DisplayHeight) / 120;

    if (static_cast<size_t>(ROI.Type) >= (sizeof(ROI_Widgets) / sizeof(ROI_Widgets[0]))) {
        ESP_LOGW(TAG, "Invalid GUI ROI type: 0x%X", ROI.Type);

        return;
    }

    lv_obj_t *ROI_Widget = *ROI_Widgets[ROI.Type];
    if (ROI_Widget != NULL) {
        lv_obj_set_align(ROI_Widget, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(ROI_Widget, DispX, DispY);
        lv_obj_set_size(ROI_Widget, DispW, DispH);
    }
}

void GUI_ROI_Save(Settings_ROI_t ROI)
{
    Settings_Lepton_t SettingsLepton;

    /* Copy the new ROI in the existing settings structure. */
    memcpy(&SettingsLepton.ROI[ROI.Type], &ROI, sizeof(Settings_ROI_t));

    // TODO: Get the dimensions from the ROI widgets

    /* Save the new ROI to NVS. */
    // TODO: Fix me. Causes crashes during boot
    //SettingsManager_UpdateLepton(&SettingsLepton);
    SettingsManager_Save();

    /* The Lepton task needs the ROI with the Lepton coordinates. */
    SettingsLepton.ROI[ROI.Type] = {
        .Type = ROI.Type,
        .x = static_cast<uint16_t>(ROI.x),
        .y = static_cast<uint16_t>(ROI.y),
        .w = static_cast<uint16_t>(ROI.w),
        .h = static_cast<uint16_t>(ROI.h)
    };

    /* Post the ROI changed event to notify the Lepton task about the new ROI */
    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_ROI, &SettingsLepton.ROI[ROI.Type], sizeof(Settings_ROI_t),
                   pdMS_TO_TICKS(100));
}