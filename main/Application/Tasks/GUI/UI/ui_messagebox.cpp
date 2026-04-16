/*
 * ui_messagenbox.cpp
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

#include <esp_err.h>
#include <esp_log.h>

#include "ui_messagebox.h"

static const char *TAG = "ui_messagenbox";

static lv_obj_t *_p_ProgressBox = NULL;

static void MessageBox_on_Close(lv_timer_t *p_Timer)
{
    lv_obj_t *Box = static_cast<lv_obj_t *>(lv_timer_get_user_data(p_Timer));

    lv_msgbox_close(Box);
}

void MessageBox_Show(const char *p_Title, uint32_t AutoCloseDelay)
{
    lv_obj_t *Box = lv_msgbox_create(NULL);

    lv_msgbox_add_title(Box, p_Title);

    /* Auto-close message box after specified delay */
    lv_timer_t *Timer = lv_timer_create(MessageBox_on_Close, AutoCloseDelay * 1000, Box);
    lv_timer_set_repeat_count(Timer, 1);
}

void MessageBox_ShowProgress(const char *p_Title)
{
    _p_ProgressBox = lv_msgbox_create(NULL);

    lv_msgbox_add_title(_p_ProgressBox, p_Title);
}

void MessageBox_CloseProgress(void)
{
    if (_p_ProgressBox == NULL) {
        return;
    }

    lv_msgbox_close(_p_ProgressBox);
    _p_ProgressBox = NULL;
}

void MessageBox_ImageSaveError(esp_err_t Error)
{
    lv_obj_t *Box = lv_msgbox_create(NULL);

    if (Error == ESP_OK) {
        lv_msgbox_add_title(Box, "Image Saved");
        lv_msgbox_add_text(Box, "Image saved to storage");

        ESP_LOGD(TAG, "Image saved successfully");
    } else if (Error == ESP_ERR_INVALID_STATE) {
        lv_msgbox_add_title(Box, "USB Active");
        lv_msgbox_add_text(Box, "Cannot save - USB mode is active!\nDisable USB first.");

        ESP_LOGW(TAG, "Cannot save image - USB mode active");
    } else if (Error == ESP_ERR_NO_MEM) {
        lv_msgbox_add_title(Box, "No Frame");
        lv_msgbox_add_text(Box, "No image frame available");

        ESP_LOGW(TAG, "No image frame available");
    } else {
        lv_msgbox_add_title(Box, "Save Failed");
        lv_msgbox_add_text(Box, "Failed to save image");

        ESP_LOGE(TAG, "Failed to save image: 0x%X!", Error);
    }

    /* Auto-close message box after 2 seconds */
    lv_timer_t *Timer = lv_timer_create(MessageBox_on_Close, 2000, Box);
    lv_timer_set_repeat_count(Timer, 1);
}
