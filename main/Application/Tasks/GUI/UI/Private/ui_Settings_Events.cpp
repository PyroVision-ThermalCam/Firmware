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

#include "ui_Settings_Events.h"

static const char *TAG = "ui_Settings_Events";

void on_Lepton_Emissivity_Slider_Callback(lv_event_t * e) {
    int Value;
    App_Settings_Lepton_t LeptonSettings;
    lv_obj_t * slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    Slider_Widgets_t * widgets = static_cast<Slider_Widgets_t*>(lv_obj_get_user_data(slider));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = static_cast<int>(lv_slider_get_value(slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Save on release only */
    if(lv_event_get_code(e) == LV_EVENT_RELEASED) {
        SettingsManager_Setting_t Setting;

        Setting.ID = SETTINGS_ID_LEPTON_EMISSIVITY;
        Setting.Value = Value;
        LeptonSettings.CurrentEmissivity = static_cast<uint8_t>(Value);
        SettingsManager_UpdateLepton(&LeptonSettings, &Setting);
    }
}

void on_Display_Brightness_Slider_Callback(lv_event_t * e) {
    int Value;
    App_Settings_Display_t DisplaySettings;
    lv_obj_t * slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    Slider_Widgets_t * widgets = static_cast<Slider_Widgets_t*>(lv_obj_get_user_data(slider));

    SettingsManager_GetDisplay(&DisplaySettings);

    Value = static_cast<int>(lv_slider_get_value(slider));
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Save on release only */
    if(lv_event_get_code(e) == LV_EVENT_RELEASED) {
        DisplaySettings.Brightness = static_cast<uint8_t>(Value);
        SettingsManager_UpdateDisplay(&DisplaySettings);
    }
}

void on_Lepton_Dropdown_Callback(lv_event_t * e) {
    int Value;
    App_Settings_Lepton_t LeptonSettings;
    lv_obj_t * dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    Slider_Widgets_t * widgets = static_cast<Slider_Widgets_t*>(lv_obj_get_user_data(dropdown));

    SettingsManager_GetLepton(&LeptonSettings);

    Value = LeptonSettings.EmissivityPresets[lv_dropdown_get_selected(dropdown)].Value * 100;
    lv_slider_set_value(widgets->Slider, Value, LV_ANIM_ON);
    lv_label_set_text_fmt(widgets->Label, "%d", Value);

    /* Save settings directly without triggering additional events */
    SettingsManager_Setting_t Setting;
    Setting.ID = SETTINGS_ID_LEPTON_EMISSIVITY;
    Setting.Value = Value;
    LeptonSettings.CurrentEmissivity = static_cast<uint8_t>(Value);
    SettingsManager_UpdateLepton(&LeptonSettings, &Setting);
}

void on_WiFi_Autoconnect_Callback(lv_event_t * e) {
    App_Settings_WiFi_t WiFiSettings;
    lv_obj_t * switch_obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

    SettingsManager_GetWiFi(&WiFiSettings);

    if(lv_obj_has_state(switch_obj, LV_STATE_CHECKED)) {
        WiFiSettings.AutoConnect = true;
        ESP_LOGI(TAG, "WiFi autoconnect enabled");
    } else {
        WiFiSettings.AutoConnect = false;
        ESP_LOGI(TAG, "WiFi autoconnect disabled");
    }

    SettingsManager_UpdateWiFi(&WiFiSettings);
}

void on_WiFi_Connect_Callback(lv_event_t * e)
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
            SettingsManager_Setting_t NewSetting;

            memcpy(&NewSetting, p_Data, sizeof(SettingsManager_Setting_t));

            ESP_LOGD(TAG, "Lepton settings changed: ID=%d", NewSetting.ID);
            ESP_LOGD(TAG, "Lepton settings changed: Value=%d", NewSetting.Value);

            if(NewSetting.ID == SETTINGS_ID_LEPTON_EMISSIVITY) {
                lv_slider_set_value(emissivity_widgets.Slider, NewSetting.Value, LV_ANIM_ON);
                lv_label_set_text_fmt(emissivity_widgets.Label, "%d", static_cast<int>(NewSetting.Value));
            }

            break;
        }
        case SETTINGS_EVENT_WIFI_CHANGED: {
            ESP_LOGI(TAG, "WiFi settings changed.");

            break;
        }
    }
}

void on_Flash_ClearNVS_Callback(lv_event_t * e)
{
    ESP_LOGI(TAG, "Resetting settings to factory defaults...");

    esp_err_t Error = SettingsManager_ResetToDefaults();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Settings reset successfully, restarting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to reset settings: %d!", Error);
    }
}

void on_Flash_ClearStorage_Callback(lv_event_t * e)
{
    ESP_LOGI(TAG, "Erasing storage partition...");

    esp_err_t Error = MemoryManager_EraseStorage();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Storage partition erased successfully");
        /* Update UI to show 0 bytes used */
        ui_settings_update_flash_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase storage partition: %d!", Error);
    }
}

void on_Flash_ClearCoredump_Callback(lv_event_t * e)
{
    ESP_LOGI(TAG, "Erasing coredump partition...");

    esp_err_t Error = MemoryManager_EraseCoredump();
    if (Error == ESP_OK) {
        ESP_LOGI(TAG, "Coredump partition erased successfully");
        /* Update UI to show 0 bytes used */
        ui_settings_update_flash_usage();
    } else {
        ESP_LOGE(TAG, "Failed to erase coredump partition: %d!", Error);
    }
}