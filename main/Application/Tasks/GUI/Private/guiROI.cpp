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

const char *ROI_Labels[] = {
    "Spotmeter",
    "Scene",
    "AGC",
    "Video Focus"
};

/** @brief      Callback function for the ROI fading animation.
 *  @param obj  LVGL object being animated
 *  @param v    Current opacity value
 */
static void GUI_ROI_Fading_Callback(void * obj, int32_t v)
{
    lv_obj_set_style_opa(reinterpret_cast<lv_obj_t *>(obj), v, 0);
}

/** @brief          Start the ROI fading animation.
 *  @param ROI      ROI index
 */
static void GUI_ROI_StartFadingAnimation(uint8_t ROI)
{
    lv_obj_t *ROI_Widget = *ROI_Widgets[ROI];

    lv_anim_init(&_GUITaskState.ROIConfig.FadingAnimation);
    lv_anim_set_var(&_GUITaskState.ROIConfig.FadingAnimation, ROI_Widget);
    lv_anim_set_exec_cb(&_GUITaskState.ROIConfig.FadingAnimation, GUI_ROI_Fading_Callback);
    lv_anim_set_values(&_GUITaskState.ROIConfig.FadingAnimation, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&_GUITaskState.ROIConfig.FadingAnimation, 500);
    lv_anim_set_playback_time(&_GUITaskState.ROIConfig.FadingAnimation, 500);
    lv_anim_set_repeat_count(&_GUITaskState.ROIConfig.FadingAnimation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&_GUITaskState.ROIConfig.FadingAnimation);
}

/** @brief          Draw the specified ROI on the display.
 *                  NOTE: The ROI position and size are specified in the Lepton's native 160x120 coordinate space and scaled to the display size.
 *  @param ROI      ROI index
 *  @param NewX     New X position
 *  @param NewY     New Y position
 *  @param NewW     New width
 *  @param NewH     New height
 */
static void GUI_ROI_Draw(uint8_t ROI, int16_t NewX, int16_t NewY, int16_t NewW, int16_t NewH)
{
    lv_obj_t *ROI_Widget = *ROI_Widgets[ROI];

    /* Update widget position and size on display.
     * LV_ALIGN_TOP_LEFT is required so that LVGL treats the coordinates as an
     * absolute offset from the parent's top-left origin. */
    lv_obj_set_align(ROI_Widget, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ROI_Widget, (NewX * _GUITaskState.ROIConfig.DisplayWidth) / 160, (NewY * _GUITaskState.ROIConfig.DisplayHeight) / 120);
    lv_obj_set_size(ROI_Widget, (NewW * _GUITaskState.ROIConfig.DisplayWidth) / 160, (NewH * _GUITaskState.ROIConfig.DisplayHeight) / 120);

    ESP_LOGD(TAG, "ROI %u changed to (%" PRId32 ", %" PRId32 ") size %" PRId32 "x%" PRId32,
             _GUITaskState.ROIConfig.SelectedROI, NewX, NewY, NewW, NewH);
}

void GUI_ROI_Init(void)
{
    lv_obj_t *ROI_Widget;

    /* Enable ROI configuration mode */
    _GUITaskState.ROIConfig.IsActive = true;

    /* Always start with size change mode when ROI configuration mode is activated */
    _GUITaskState.ROIConfig.IsMovingActive = false;

    _GUITaskState.ROIConfig.DisplayWidth = lv_obj_get_width(ui_Image_Main_Image);
    _GUITaskState.ROIConfig.DisplayHeight = lv_obj_get_height(ui_Image_Main_Image);

    /* Hide all ROIs initially */
    for (int i = 0; i < (sizeof(ROI_Widgets) / sizeof(ROI_Widgets[0])); i++) {
        lv_obj_add_flag(*ROI_Widgets[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Always start with the first ROI when ROI configuration mode is activated */
    _GUITaskState.ROIConfig.SelectedROI = 0;

    /* Show the first ROI */
    ROI_Widget = *ROI_Widgets[_GUITaskState.ROIConfig.SelectedROI];
    lv_obj_remove_flag(ROI_Widget, LV_OBJ_FLAG_HIDDEN);

    GUI_ROI_StartFadingAnimation(_GUITaskState.ROIConfig.SelectedROI);

    /* Set the ROI name */
    lv_label_set_text(ui_Label_Main_ROI_Name, ROI_Labels[_GUITaskState.ROIConfig.SelectedROI]);

    /* Change the label of button 1 to "Back" */
    lv_label_set_text(ui_Label_Main_Button1, "\uF060");

    /* Change the label of button 2 to "Rotate" */
    lv_label_set_text(ui_Label_Main_Button2, "\uF2F1");

    /* Change the label of button 3 to "Resize" */
    lv_label_set_text(ui_Label_Main_Button3, "\uF065");

    /* Change the label of button 4 to "Reset" */
    lv_label_set_text(ui_Label_Main_Button4, "\uF0E2");
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

void GUI_ROI_SwitchToROI(uint8_t ROI)
{
    lv_obj_t *ROI_Widget;

    /* Hide the current ROI */
    lv_obj_add_flag(*ROI_Widgets[_GUITaskState.ROIConfig.SelectedROI], LV_OBJ_FLAG_HIDDEN);

    _GUITaskState.ROIConfig.SelectedROI = (_GUITaskState.ROIConfig.SelectedROI + 1) % 4;

    /* Show the new ROI */
    ROI_Widget = *ROI_Widgets[_GUITaskState.ROIConfig.SelectedROI];
    lv_obj_remove_flag(ROI_Widget, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(ui_Label_Main_ROI_Name, ROI_Labels[_GUITaskState.ROIConfig.SelectedROI]);

    GUI_ROI_StartFadingAnimation(_GUITaskState.ROIConfig.SelectedROI);
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

void GUI_ROI_Save(void)
{
    SettingsManager_Save();

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_ROI_CHANGED, NULL, 0, pdMS_TO_TICKS(100));
}

void GUI_ROI_Change(int16_t DeltaX, int16_t DeltaY, int16_t DeltaW, int16_t DeltaH)
{
    int32_t NewX;
    int32_t NewY;
    int32_t NewW;
    int32_t NewH;
    Settings_Lepton_t LeptonSettings;

    SettingsManager_GetLepton(&LeptonSettings);

    NewX = LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].x + DeltaX;
    NewY = LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].y + DeltaY;
    NewW = LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].w + DeltaW;
    NewH = LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].h + DeltaH;

    /* Clamp size to minimum 1x1 sensor pixel */
    if (NewW < 1) {
        NewW = 1;
    }

    if (NewH < 1) {
        NewH = 1;
    }

    /* Clamp position to sensor bounds (160 x 120) */
    if (NewX < 0) {
        NewX = 0;
    }

    if (NewY < 0) {
        NewY = 0;
    }

    /* Keep ROI fully within sensor area; prefer clamping the moved edge */
    if ((NewX + NewW) > 160) {
        if (DeltaX != 0) {
            NewX = 160 - NewW;
        } else {
            NewW = 160 - NewX;
        }
    }

    if ((NewY + NewH) > 120) {
        if (DeltaY != 0) {
            NewY = 120 - NewH;
        } else {
            NewH = 120 - NewY;
        }
    }

    /* Persist updated position and size in settings RAM */
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].x = static_cast<int16_t>(NewX);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].y = static_cast<int16_t>(NewY);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].w = static_cast<int16_t>(NewW);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].h = static_cast<int16_t>(NewH);
    SettingsManager_UpdateLepton(&LeptonSettings, NULL);

    /* Settings_Lepton_t is now off the stack; update the widget */
    GUI_ROI_Draw(_GUITaskState.ROIConfig.SelectedROI, static_cast<int16_t>(NewX), static_cast<int16_t>(NewY), static_cast<int16_t>(NewW), static_cast<int16_t>(NewH));
}

void GUI_ROI_Reset(void)
{
    int32_t NewX;
    int32_t NewY;
    int32_t NewW;
    int32_t NewH;
    Settings_Lepton_t LeptonSettings;

    ESP_LOGD(TAG, "Resetting ROI %u to default position and size", _GUITaskState.ROIConfig.SelectedROI);

    switch (_GUITaskState.ROIConfig.SelectedROI) {
        case 0: {
            NewW = 60;
            NewH = 60;
            NewX = (160 - NewW) / 2;
            NewY = (120 - NewH) / 2;

            break;
        }
        case 1: {
            NewX = 0;
            NewY = 0;
            NewW = 160;
            NewH = 120;

            break;
        }
        case 2: {
            NewX = 0;
            NewY = 0;
            NewW = 160;
            NewH = 120;

            break;
        }
        case 3: {
            NewX = 1;
            NewY = 1;
            NewW = 158;
            NewH = 118;

            break;
        }
        default: {
            ESP_LOGE(TAG, "Invalid ROI index %u for reset", _GUITaskState.ROIConfig.SelectedROI);

            return;
        }
    }

    SettingsManager_GetLepton(&LeptonSettings);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].x = static_cast<int16_t>(NewX);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].y = static_cast<int16_t>(NewY);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].w = static_cast<int16_t>(NewW);
    LeptonSettings.ROI[_GUITaskState.ROIConfig.SelectedROI].h = static_cast<int16_t>(NewH);
    SettingsManager_UpdateLepton(&LeptonSettings, NULL);

    GUI_ROI_Draw(_GUITaskState.ROIConfig.SelectedROI, static_cast<int16_t>(NewX), static_cast<int16_t>(NewY), static_cast<int16_t>(NewW), static_cast<int16_t>(NewH));
}

void GUI_ROI_ResetAll(void)
{
    for (uint8_t i = 0; i < 4; i++) {
        _GUITaskState.ROIConfig.SelectedROI = i;
        GUI_ROI_Reset();
    }
}