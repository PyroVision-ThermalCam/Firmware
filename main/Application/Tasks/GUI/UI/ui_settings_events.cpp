/*
 * ui_Settings_Events.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings UI implementation.
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

#include <string.h>

#include "managers.h"

#include "ui_messagebox.h"
#include "ui_settings_events.h"

static const char *TAG = "ui_settings_events";

void on_Lepton_Emissivity_Slider_Callback(lv_event_t *e)
{
    int Value;
    Settings_Lepton_t LeptonSettings;
    lv_obj_t *Slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *Widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(Slider));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = static_cast<int>(lv_slider_get_value(Slider));
    lv_label_set_text_fmt(Widgets->Label, "%d", Value);

    /* Save on release only */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        SettingsManager_ChangeNotification_t Changed;

        Changed.ID = SETTINGS_ID_LEPTON_EMISSIVITY;
        Changed.Value = Value;
        LeptonSettings.CurrentEmissivity = static_cast<uint8_t>(Value);
        SettingsManager_UpdateLepton(&LeptonSettings, &Changed);
    }
}

void on_Display_Brightness_Slider_Callback(lv_event_t *e)
{
    int Value;
    Settings_Display_t DisplaySettings;
    lv_obj_t *Slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *Widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(Slider));

    SettingsManager_GetDisplay(&DisplaySettings);

    Value = static_cast<int>(lv_slider_get_value(Slider));
    lv_label_set_text_fmt(Widgets->Label, "%d", Value);

    /* Save on release only */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        SettingsManager_ChangeNotification_t Changed;

        Changed.ID = SETTINGS_ID_DISPLAY_BRIGHTNESS;
        Changed.Value = Value;
        DisplaySettings.Brightness = static_cast<uint8_t>(Value);
        SettingsManager_UpdateDisplay(&DisplaySettings, &Changed);
    }
}

void on_Lepton_Dropdown_Callback(lv_event_t *e)
{
    int Value;
    Settings_Lepton_t LeptonSettings;
    SettingsManager_ChangeNotification_t Changed;
    lv_obj_t *Dropdown = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *Widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(Dropdown));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = LeptonSettings.EmissivityPresets[lv_dropdown_get_selected(Dropdown)].Value * 100;
    lv_slider_set_value(Widgets->Slider, Value, LV_ANIM_ON);
    lv_label_set_text_fmt(Widgets->Label, "%d", Value);

    /* Save settings directly without triggering additional events */
    Changed.ID = SETTINGS_ID_LEPTON_EMISSIVITY;
    Changed.Value = Value;
    LeptonSettings.CurrentEmissivity = static_cast<uint8_t>(Value);
    SettingsManager_UpdateLepton(&LeptonSettings, &Changed);
}

void on_WiFi_Autoconnect_Callback(lv_event_t *e)
{
    Settings_WiFi_t WiFiSettings;
    lv_obj_t *switch_obj = static_cast<lv_obj_t *>(lv_event_get_target(e));

    SettingsManager_GetWiFi(&WiFiSettings);

    if (lv_obj_has_state(switch_obj, LV_STATE_CHECKED)) {
        WiFiSettings.AutoConnect = true;

        ESP_LOGD(TAG, "WiFi autoconnect enabled");
    } else {
        WiFiSettings.AutoConnect = false;

        ESP_LOGD(TAG, "WiFi autoconnect disabled");
    }

    SettingsManager_UpdateWiFi(&WiFiSettings);
}

void on_WiFi_Connect_Callback(lv_event_t *e)
{
    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_OPEN_WIFI_REQUEST, NULL, 0, pdMS_TO_TICKS(100));
}

void on_WiFi_ClearCredentials_Callback(lv_event_t *e)
{
    Settings_WiFi_t WiFiSettings;

    SettingsManager_GetWiFi(&WiFiSettings);

    memset(WiFiSettings.SSID, '\0', sizeof(WiFiSettings.SSID));
    memset(WiFiSettings.Password, '\0', sizeof(WiFiSettings.Password));

    SettingsManager_UpdateWiFi(&WiFiSettings);
    SettingsManager_Save();

    ESP_LOGD(TAG, "WiFi credentials cleared");

    MessageBox_Show("Credentials cleared", 2);
}

void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Network event received: ID=%d", ID);

    switch (ID) {
        case NETWORK_EVENT_WIFI_GOT_IP: {
            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            break;
        }
    }
}

void on_Settings_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Settings event received: ID=%d", ID);

    switch (ID) {
        case SETTINGS_EVENT_LEPTON_CHANGED: {
            /*
            SettingsManager_ChangeNotification_t Changed;

            memcpy(&Changed, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGD(TAG, "Lepton settings changed: ID=%d", Changed.ID);
            ESP_LOGD(TAG, "Lepton settings changed: Value=%d", Changed.Value);

            if (Changed.ID == SETTINGS_ID_LEPTON_EMISSIVITY) {
                lv_slider_set_value(emissivity_widgets.Slider, Changed.Value, LV_ANIM_ON);
                lv_label_set_text_fmt(emissivity_widgets.Label, "%d", static_cast<int>(Changed.Value));
            }
            */
            break;
        }
        case SETTINGS_EVENT_WIFI_CHANGED: {
            ESP_LOGI(TAG, "WiFi settings changed.");

            break;
        }
    }
}

void on_USB_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "USB event received: ID=%d", ID);

    switch (ID) {
        case USB_EVENT_CABLE_CONNECTED: {
            lv_obj_remove_state(usb_mode_switch, LV_STATE_DISABLED);
            lv_obj_remove_state(usb_uvc_switch, LV_STATE_DISABLED);
            lv_obj_clear_state(usb_mode_switch, LV_STATE_CHECKED);
            lv_obj_clear_state(usb_uvc_switch, LV_STATE_CHECKED);

            ESP_LOGD(TAG, "USB cable connected, switches enabled");

            break;
        }
        case USB_EVENT_CABLE_DISCONNECTED: {
            lv_obj_add_state(usb_mode_switch, LV_STATE_DISABLED);
            lv_obj_add_state(usb_uvc_switch, LV_STATE_DISABLED);
            lv_obj_clear_state(usb_mode_switch, LV_STATE_CHECKED);
            lv_obj_clear_state(usb_uvc_switch, LV_STATE_CHECKED);

            ESP_LOGD(TAG, "USB cable disconnected, switches disabled");

            break;
        }
        case USB_EVENT_INITIALIZED: {
            ESP_LOGD(TAG, "USB subsystem initialized");

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            lv_obj_add_state(usb_mode_switch, LV_STATE_DISABLED);
            lv_obj_add_state(usb_uvc_switch, LV_STATE_DISABLED);
            lv_obj_clear_state(usb_mode_switch, LV_STATE_CHECKED);
            lv_obj_clear_state(usb_uvc_switch, LV_STATE_CHECKED);

            break;
        }
    }
}

/** @brief POSIX timezone strings corresponding to the dropdown entries in
 *         ui_Settings_Create_System_Page().  The index must match the
 *         dropdown option index exactly.
 */
static const char *TIMEZONE_POSIX[] = {
    "UTC0",                                     /* UTC */
    "GMT0BST,M3.5.0/1,M10.5.0",                /* Europe/London */
    "CET-1CEST,M3.5.0,M10.5.0/3",             /* Europe/Berlin / Paris */
    "EET-2EEST,M3.5.0/3,M10.5.0/4",           /* Europe/Helsinki */
    "MSK-3",                                    /* Europe/Moscow */
    "EST5EDT,M3.2.0,M11.1.0",                  /* America/New_York */
    "CST6CDT,M3.2.0,M11.1.0",                  /* America/Chicago */
    "MST7MDT,M3.2.0,M11.1.0",                  /* America/Denver */
    "PST8PDT,M3.2.0,M11.1.0",                  /* America/Los_Angeles */
    "JST-9",                                    /* Asia/Tokyo */
    "CST-8",                                    /* Asia/Shanghai */
    "IST-5:30",                                 /* Asia/Kolkata */
    "AEST-10AEDT,M10.1.0,M4.1.0/3",           /* Australia/Sydney */
};

void on_System_Timezone_Callback(lv_event_t *e)
{
    Settings_System_t SystemSettings;
    uint16_t Selected;

    Selected = lv_dropdown_get_selected(static_cast<lv_obj_t *>(lv_event_get_target(e)));

    if (Selected >= (sizeof(TIMEZONE_POSIX) / sizeof(TIMEZONE_POSIX[0]))) {
        ESP_LOGW(TAG, "Invalid timezone index: %d", Selected);

        return;
    }

    SettingsManager_GetSystem(&SystemSettings);

    strncpy(SystemSettings.Timezone, TIMEZONE_POSIX[Selected], sizeof(SystemSettings.Timezone) - 1);
    SystemSettings.Timezone[sizeof(SystemSettings.Timezone) - 1] = '\0';

    SettingsManager_UpdateSystem(&SystemSettings, NULL);

    TimeManager_SetTimezone(SystemSettings.Timezone);

    ESP_LOGD(TAG, "Timezone set to: %s", SystemSettings.Timezone);
}

void on_Lepton_Palette_Callback(lv_event_t *e)
{
    Settings_Lepton_t LeptonSettings;
    SettingsManager_ChangeNotification_t Changed;
    uint16_t Selected = lv_dropdown_get_selected(static_cast<lv_obj_t *>(lv_event_get_target(e)));

    SettingsManager_GetLepton(&LeptonSettings);

    LeptonSettings.Palette = static_cast<uint8_t>(Selected);
    Changed.ID = SETTINGS_ID_LEPTON_PALETTE;
    Changed.Value = static_cast<uint32_t>(Selected);
    SettingsManager_UpdateLepton(&LeptonSettings, &Changed);

    ESP_LOGD(TAG, "Palette changed to index: %u", static_cast<unsigned int>(Selected));
}

void on_Memory_ClearNVS_Callback(lv_event_t *e){
    esp_err_t Error;

    ESP_LOGD(TAG, "Resetting settings to factory defaults...");

    Error = SettingsManager_ResetToDefaults();
    if (Error == ESP_OK) {
        ESP_LOGD(TAG, "Settings reset successfully, restarting...");

        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to reset settings: 0x%X!", Error);
    }
}

void on_Image_Format_Dropdown_Callback(lv_event_t *e)
{
    Settings_System_t SystemSettings;

    SettingsManager_GetSystem(&SystemSettings);

    SystemSettings.ImageFormat = static_cast<ImageEncoder_Format_t>(lv_dropdown_get_selected(static_cast<lv_obj_t *>
                                                                                               (lv_event_get_target(e))));
    SettingsManager_UpdateSystem(&SystemSettings, NULL);

    if (SystemSettings.ImageFormat == IMAGE_FORMAT_JPEG) {
        lv_obj_remove_flag(jpeg_quality_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(jpeg_quality_row, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGD(TAG, "Image format changed to: 0x%X", SystemSettings.ImageFormat);
}

void on_Image_JpegQuality_Slider_Callback(lv_event_t *e)
{
    int Value;
    Settings_System_t SystemSettings;
    lv_obj_t *Slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(Slider));

    SettingsManager_GetSystem(&SystemSettings);

    Value = static_cast<int>(lv_slider_get_value(Slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    SystemSettings.JpegQuality = static_cast<uint8_t>(Value);
    SettingsManager_UpdateSystem(&SystemSettings, NULL);
}

void on_Memory_ClearStorage_Callback(lv_event_t *e)
{
    esp_err_t Error;

    ESP_LOGD(TAG, "Erasing storage partition...");

    Error = MemoryManager_EraseStorage();
    if (Error == ESP_OK) {
        ESP_LOGD(TAG, "Storage partition erased successfully");

        ui_settings_update_memory_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase storage partition: 0x%X!", Error);
    }
}

void on_Memory_ClearCoredump_Callback(lv_event_t *e)
{
    esp_err_t Error;

    ESP_LOGD(TAG, "Erasing coredump partition...");

    Error = MemoryManager_EraseCoredump();
    if (Error == ESP_OK) {
        ESP_LOGD(TAG, "Coredump partition erased successfully");

        ui_settings_update_memory_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase coredump partition: 0x%X!", Error);
    }
}

void on_USB_Mode_Switch_Callback(lv_event_t *e)
{
    esp_err_t Error;
    lv_obj_t *Switch = static_cast<lv_obj_t *>(lv_event_get_target(e));
    bool Enable = lv_obj_has_state(Switch, LV_STATE_CHECKED);

    ESP_LOGD(TAG, "%s USB MSC...", Enable ? "Enabling" : "Disabling");

    Error = USBManager_EnableMSC(Enable);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue MSC command: 0x%X!", Error);

        if (Enable) {
            lv_obj_clear_state(Switch, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(Switch, LV_STATE_CHECKED);
        }
    }
}

void on_USB_UVC_Switch_Callback(lv_event_t *e)
{
    esp_err_t Error;
    lv_obj_t *Switch = static_cast<lv_obj_t *>(lv_event_get_target(e));
    bool Enable = lv_obj_has_state(Switch, LV_STATE_CHECKED);

    ESP_LOGD(TAG, "%s USB UVC...", Enable ? "Enabling" : "Disabling");

    Error = USBManager_EnableUVC(Enable);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue UVC command: 0x%X!", Error);

        if (Enable) {
            lv_obj_clear_state(Switch, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(Switch, LV_STATE_CHECKED);
        }
    }
}

void on_USB_CDC_Switch_Callback(lv_event_t *e)
{
    Settings_USB_t USBSettings;

    SettingsManager_GetUSB(&USBSettings);

    USBSettings.CDC_Enabled = lv_obj_has_state(static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);

    ESP_LOGD(TAG, "CDC %s in USB settings (takes effect on next USB enable).",
             USBSettings.CDC_Enabled ? "enabled" : "disabled");

    SettingsManager_UpdateUSB(&USBSettings, NULL);
}

void on_Calibration_RoomTemp_Changed_Callback(lv_event_t *e)
{
    Settings_Calibration_t CalibSettings;
    SettingsManager_ChangeNotification_t Changed;
    lv_obj_t *Spinbox = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int32_t Value = lv_spinbox_get_value(Spinbox);

    SettingsManager_GetCalibration(&CalibSettings);

    CalibSettings.RoomTemperature = static_cast<int16_t>(Value);

    Changed.ID = SETTINGS_ID_CALIBRATION_ROOM_TEMP;
    Changed.Value = static_cast<uint32_t>(Value);
    SettingsManager_UpdateCalibration(&CalibSettings, &Changed);

    ESP_LOGD(TAG, "Calibration room temperature changed to: %d \xC2\xB0""C", static_cast<int>(Value));
}
