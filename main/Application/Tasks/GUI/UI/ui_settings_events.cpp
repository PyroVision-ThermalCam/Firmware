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

#include "managers.h"

#include "ui_settings_events.h"

static USB_Manager_Config_t USB_Config;

static const char *TAG = "ui_settings_events";

void on_Lepton_Emissivity_Slider_Callback(lv_event_t *e)
{
    int Value;
    Settings_Lepton_t LeptonSettings;
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(slider));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = static_cast<int>(lv_slider_get_value(slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

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
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(slider));

    SettingsManager_GetDisplay(&DisplaySettings);

    Value = static_cast<int>(lv_slider_get_value(slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Save on release only */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        DisplaySettings.Brightness = static_cast<uint8_t>(Value);
        SettingsManager_UpdateDisplay(&DisplaySettings);
    }
}

void on_Lepton_Dropdown_Callback(lv_event_t *e)
{
    int Value;
    Settings_Lepton_t LeptonSettings;
    lv_obj_t *dropdown = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(dropdown));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = LeptonSettings.EmissivityPresets[lv_dropdown_get_selected(dropdown)].Value * 100;
    lv_slider_set_value(widgets->Slider, Value, LV_ANIM_ON);
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Save settings directly without triggering additional events */
    SettingsManager_ChangeNotification_t Changed;
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

        ESP_LOGI(TAG, "WiFi autoconnect enabled");
    } else {
        WiFiSettings.AutoConnect = false;

        ESP_LOGI(TAG, "WiFi autoconnect disabled");
    }

    SettingsManager_UpdateWiFi(&WiFiSettings);
}

void on_WiFi_Connect_Callback(lv_event_t *e)
{
    ESP_LOGI(TAG, "WiFi connect button clicked, posting network event...");

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_OPEN_WIFI_REQUEST, NULL, 0, 0);
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
        case USB_EVENT_INITIALIZED: {
            lv_obj_add_state(usb_mode_switch, LV_STATE_CHECKED);

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            lv_obj_clear_state(usb_mode_switch, LV_STATE_CHECKED);

            break;
        }
    }
}

void on_Flash_ClearNVS_Callback(lv_event_t *e)
{
    esp_err_t Error;

    ESP_LOGI(TAG, "Resetting settings to factory defaults...");

    Error = SettingsManager_ResetToDefaults();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Settings reset successfully, restarting...");

        vTaskDelay(500 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to reset settings: %d!", Error);
    }
}

void on_Image_Format_Dropdown_Callback(lv_event_t *e)
{
    Settings_System_t SystemSettings;
    lv_obj_t *dropdown = static_cast<lv_obj_t *>(lv_event_get_target(e));
    uint16_t selected = lv_dropdown_get_selected(dropdown);

    SettingsManager_GetSystem(&SystemSettings);

    /* Update image format */
    SystemSettings.ImageFormat = static_cast<Settings_Image_Format_t>(selected);
    SettingsManager_UpdateSystem(&SystemSettings, NULL);

    /* Show/hide JPEG quality slider based on format */
    if (SystemSettings.ImageFormat == IMAGE_FORMAT_JPEG) {
        lv_obj_remove_flag(jpeg_quality_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lv_obj_get_parent(jpeg_quality_row), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(jpeg_quality_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lv_obj_get_parent(jpeg_quality_row), LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "Image format changed to: %d", SystemSettings.ImageFormat);
}

void on_Image_JpegQuality_Slider_Callback(lv_event_t *e)
{
    int Value;
    Settings_System_t SystemSettings;
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    Slider_Widgets_t *widgets = static_cast<Slider_Widgets_t *>(lv_obj_get_user_data(slider));

    SettingsManager_GetSystem(&SystemSettings);

    Value = static_cast<int>(lv_slider_get_value(slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Update immediately for live preview */
    SystemSettings.JpegQuality = static_cast<uint8_t>(Value);
    SettingsManager_UpdateSystem(&SystemSettings, NULL);
}

void on_Flash_ClearStorage_Callback(lv_event_t *e)
{
    esp_err_t Error;

    ESP_LOGI(TAG, "Erasing storage partition...");

    Error = MemoryManager_EraseStorage();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Storage partition erased successfully");

        ui_settings_update_flash_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase storage partition: %d!", Error);
    }
}

void on_Flash_ClearCoredump_Callback(lv_event_t *e)
{
    esp_err_t Error;

    ESP_LOGI(TAG, "Erasing coredump partition...");

    Error = MemoryManager_EraseCoredump();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Coredump partition erased successfully");

        ui_settings_update_flash_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase coredump partition: %d!", Error);
    }
}

void on_USB_Mode_Switch_Callback(lv_event_t *e)
{
    esp_err_t Error;
    lv_obj_t *switch_obj = static_cast<lv_obj_t *>(lv_event_get_target(e));

    if (lv_obj_has_state(switch_obj, LV_STATE_CHECKED)) {
        Settings_Info_t InfoSettings;

        ESP_LOGI(TAG, "Enabling USB Mass Storage...");

        if (USBManager_IsInitialized()) {
            ESP_LOGW(TAG, "USB already active!");

            return;
        }

        SettingsManager_GetInfo(&InfoSettings);

        /* Configure USB Mass Storage with auto-detected mount point */
        USB_Config = {
            .MountPoint = MemoryManager_GetStoragePath(),
            .Manufacturer = InfoSettings.Manufacturer,
            .Product = InfoSettings.Name,
            .SerialNumber = InfoSettings.Serial,
        };

        Error = USBManager_Init(&USB_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB Manager: %d", Error);

            lv_obj_clear_state(switch_obj, LV_STATE_CHECKED);
        } else {
            ESP_LOGI(TAG, "\u2713 USB Mass Storage Device active!");
            ESP_LOGW(TAG, "\u26a0\ufe0f  Application CANNOT write to /storage while USB is active!");
        }
    } else {
        ESP_LOGI(TAG, "Disabling USB Mass Storage...");

        if (USBManager_IsInitialized() == false) {
            ESP_LOGW(TAG, "USB not active!");

            lv_obj_clear_state(switch_obj, LV_STATE_CHECKED);

            return;
        }

        USBManager_Deinit();
    }
}
