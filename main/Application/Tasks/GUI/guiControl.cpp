/*
 * guiControl.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task control implementation.
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
#include <esp_event.h>

#include "Private/guiHelper.h"
#include "Private/guiROI.h"
#include "Private/guiImageSave.h"
#include "guiTask.h"
#include "guiControl.h"

#include "UI/ui_messagebox.h"

extern GUI_Task_State_t _GUITaskState;

static const char *TAG = "GUI-ROI";

void GUI_Control_HandleButton1(void)
{
    /* Quit the ROI config menu if active */
    if (_GUITaskState.ROIConfig.IsActive == true) {
        /* Save the modified ROI */
        GUI_ROI_Save();

        GUI_ROI_Deinit();
    }
    /* Switch to menu screen if not */
    else {
        lv_screen_load(ui_Menu);
    }
}

void GUI_Control_HandleButton2Short(void)
{
    /* Switch ROI when ROI config mode is active */
    if (_GUITaskState.ROIConfig.IsActive) {
        /* Stop the current fading animation and restore full opacity */
        lv_anim_del(_GUITaskState.ROIConfig.FadingAnimation.var, NULL);
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(_GUITaskState.ROIConfig.FadingAnimation.var), LV_OPA_COVER, 0);

        GUI_ROI_SwitchToROI(_GUITaskState.ROIConfig.SelectedROI);
    }
    /* Handle ROI display logic otherwise */
    else {
        /* RGB camera view */
        if (_GUITaskState.ShowCameraView) {
            DevicesManager_ToggleFlashEnable();
        }
        /* Thermal view */
        else {
            _GUITaskState.ShowROI = !_GUITaskState.ShowROI;

            if (_GUITaskState.ShowROI) {
                lv_obj_remove_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void GUI_Control_HandleButton2Long(void)
{
    GUI_Task_ActivateROIConfig();
}

void GUI_Control_HandleButton3(void)
{
    /* Switch between "Resize" and "Moving" when ROI config mode is active */
    if (_GUITaskState.ROIConfig.IsActive) {
        _GUITaskState.ROIConfig.IsMovingActive = !_GUITaskState.ROIConfig.IsMovingActive;

        if (_GUITaskState.ROIConfig.IsMovingActive) {
            lv_label_set_text(ui_Label_Main_Button3, "\uF047");
        } else {
            lv_label_set_text(ui_Label_Main_Button3, "\uF065");
        }
    }
    /* Switch camera view otherwise */
    else {
        _GUITaskState.ShowCameraView = !_GUITaskState.ShowCameraView;
        xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_VIEW_CHANGED);
    }
}

void GUI_Control_HandleButton4Short(void)
{
    lv_obj_t* Screen;
    esp_err_t Error;

    Screen = lv_display_get_screen_active(lv_display_get_default());
    if (Screen == ui_Menu) {
        SettingsManager_Save();
        MessageBox_Show("Settings Saved");
    } else if (Screen == ui_Main) {
        /* Reset the currently selected ROI */
        if (_GUITaskState.ROIConfig.IsActive) {
            GUI_ROI_Reset();
        }
        /* Trigger image save on next frame update */
        else {
            Error = GUI_SaveImage();
            if (Error != ESP_OK) {
                MessageBox_ImageSaveError(Error);
            } else {
                MessageBox_ShowProgress("Image save in progress");
            }
        }
    }
}

void GUI_Control_HandleButton4Long(void)
{
    /* Reset all ROIs to their default positions and sizes */
    if (_GUITaskState.ROIConfig.IsActive) {
        GUI_ROI_ResetAll();
    }
}

void GUI_Control_HandleJoystick(uint32_t Key)
{
    int8_t DeltaX = 0;
    int8_t DeltaY = 0;
    int8_t DeltaH = 0;
    int8_t DeltaW = 0;

    /* Handle joystick when ROI config mode is not active */
    if (_GUITaskState.ROIConfig.IsActive == false) {
        switch (Key) {
            case GUI_KEYPAD_JOY_CENTER: {
                if (_GUITaskState.ShowCameraView) {
                    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_FOCUS, NULL, 0, portMAX_DELAY);
                } else {
                    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_FFC, NULL, 0, portMAX_DELAY);
                }

                break;
            }
            default: {
                break;
            }
        }

        return;
    }

    switch (Key) {
        case GUI_KEYPAD_JOY_UP: {
            if (_GUITaskState.ROIConfig.IsMovingActive) {
                DeltaY = -1;
            } else {
                DeltaH = -1;
            }

            break;
        }
        case GUI_KEYPAD_JOY_DOWN: {
            if (_GUITaskState.ROIConfig.IsMovingActive) {
                DeltaY = 1;
            } else {
                DeltaH = 1;
            }

            break;
        }
        case GUI_KEYPAD_JOY_LEFT: {
            if (_GUITaskState.ROIConfig.IsMovingActive) {
                DeltaX = -1;
            } else {
                DeltaW = -1;
            }

            break;
        }
        case GUI_KEYPAD_JOY_RIGHT: {
            if (_GUITaskState.ROIConfig.IsMovingActive) {
                DeltaX = 1;
            } else {
                DeltaW = 1;
            }

            break;
        }
        case GUI_KEYPAD_JOY_CENTER: {
            break;
        }
        default: {
            break;
        }
    }

    GUI_ROI_Change(DeltaX, DeltaY, DeltaW, DeltaH);
}