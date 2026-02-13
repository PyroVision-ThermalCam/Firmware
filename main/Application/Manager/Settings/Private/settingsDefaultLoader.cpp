/*
 * settingsDefaultLoader.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Default settings loader implementation.
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
#include <esp_mac.h>
#include <esp_efuse.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <string.h>

#include "settingsLoader.h"
#include "../settingsManager.h"

static const char *TAG = "Settings-Default-Loader";

void SettingsManager_InitDefaultLeptonROIs(Settings_t *p_Settings)
{
    /* Spotmeter defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].Type = ROI_TYPE_SPOTMETER;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].x = 60;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].y = 40;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].w = 40;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].h = 40;

    /* Scene statistics defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].Type = ROI_TYPE_SCENE;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].x = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].y = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].w = 160;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].h = 120;

    /* AGC defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].Type = ROI_TYPE_AGC;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].x = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].y = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].w = 160;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].h = 120;

    /* Video focus defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].Type = ROI_TYPE_VIDEO_FOCUS;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].x = 1;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].y = 1;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].w = 157;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].h = 157;
}

void SettingsManager_InitDefaultLeptonEmissivityPresets(Settings_t *p_Settings)
{
    /* No emissiviy values available */
    p_Settings->Lepton.EmissivityPresetsCount = 1;
    p_Settings->Lepton.EmissivityPresets[0].Value = 1.0f;
    strncpy(p_Settings->Lepton.EmissivityPresets[0].Description, "Unknown",
            sizeof(p_Settings->Lepton.EmissivityPresets[0].Description));

    p_Settings->Lepton.CurrentEmissivity = SETTINGS_DEFAULT_LEPTON_EMISSIVITY;
}

void SettingsManager_InitDefaultDisplay(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default Display settings");

    p_Settings->Display.Brightness = SETTINGS_DISPLAY_DEFAULT_BRIGHTNESS;
    p_Settings->Display.Timeout = SETTINGS_DISPLAY_DEFAULT_TIMEOUT;
}

void SettingsManager_InitDefaultProvisioning(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default Provisioning settings");

    p_Settings->Provisioning.Timeout = SETTINGS_PROVISIONING_DEFAULT_TIMEOUT;
    strncpy(p_Settings->Provisioning.Name, SETTINGS_PROVISIONING_DEFAULT_NAME, sizeof(p_Settings->Provisioning.Name));
}

void SettingsManager_InitDefaultWiFi(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default WiFi settings");

    p_Settings->WiFi.AutoConnect = SETTINGS_WIFI_DEFAULT_AUTOCONNECT;
    p_Settings->WiFi.MaxRetries = SETTINGS_WIFI_DEFAULT_MAX_RETRIES;
    p_Settings->WiFi.RetryInterval = SETTINGS_WIFI_DEFAULT_RETRY_INTERVAL;
    strncpy(p_Settings->WiFi.SSID, SETTINGS_WIFI_DEFAULT_SSID, sizeof(p_Settings->WiFi.SSID));
    strncpy(p_Settings->WiFi.Password, SETTINGS_WIFI_DEFAULT_PASSWORD, sizeof(p_Settings->WiFi.Password));
}

void SettingsManager_InitDefaultSystem(Settings_t *p_Settings)
{
    uint8_t Mac[6];

    ESP_LOGW(TAG, "Loading default System settings");

    if (esp_efuse_mac_get_default(Mac) == ESP_OK) {
        snprintf(p_Settings->System.DeviceName, sizeof(p_Settings->System.DeviceName),
                 "PyroVision-%02X%02X%02X%02X%02X%02X",
                 Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    } else {
        snprintf(p_Settings->System.DeviceName, sizeof(p_Settings->System.DeviceName),
                 SETTINGS_SYSTEM_DEFAULT_DEVICENAME);
        ESP_LOGW(TAG, "Failed to get MAC address, using default name");
    }

    p_Settings->System.SDCard_AutoMount = true;
    p_Settings->System.ImageFormat = IMAGE_FORMAT_JPEG;
    p_Settings->System.JpegQuality = 80;
    strncpy(p_Settings->System.Timezone, SETTINGS_SYSTEM_DEFAULT_TIMEZONE, sizeof(p_Settings->System.Timezone));
    strncpy(p_Settings->System.NTPServer, SETTINGS_SYSTEM_DEFAULT_NTP_SERVER, sizeof(p_Settings->System.NTPServer));
}

void SettingsManager_InitDefaultLepton(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default Lepton settings");

    SettingsManager_InitDefaultLeptonROIs(p_Settings);
    SettingsManager_InitDefaultLeptonEmissivityPresets(p_Settings);
}

void SettingsManager_InitDefaultHTTPServer(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default HTTP Server settings");

    p_Settings->HTTPServer.Port = SETTINGS_DEFAULT_HTTP_PORT;
    p_Settings->HTTPServer.WSPingIntervalSec = SETTINGS_DEFAULT_WS_PING_INTERVAL;
    p_Settings->HTTPServer.MaxClients = SETTINGS_DEFAULT_HTTP_MAX_CLIENTS;
    p_Settings->HTTPServer.useCORS = SETTINGS_DEFAULT_HTTP_ENABLE_CORS;
    strncpy(p_Settings->HTTPServer.APIKey, SETTINGS_DEFAULT_HTTP_API_KEY, sizeof(p_Settings->HTTPServer.APIKey));
}

void SettingsManager_InitDefaultVISAServer(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default VISA Server settings");

    p_Settings->VISAServer.Port = SETTINGS_DEFAULT_VISA_PORT;
    p_Settings->VISAServer.Timeout = SETTINGS_DEFAULT_VISA_TIMEOUT_MS;
}

void SettingsManager_InitDefaultLEDFlash(Settings_t *p_Settings)
{
    ESP_LOGW(TAG, "Loading default LED Flash settings");

    p_Settings->LEDFlash.Enable = SETTINGS_DEFAULT_LED_FLASH_ENABLE;
    p_Settings->LEDFlash.Power = SETTINGS_DEFAULT_LED_FLASH_POWER;
}

void SettingsManager_LoadFromDefaults(Settings_Manager_State_t *p_State)
{
    memset(&p_State->Settings, 0, sizeof(Settings_t));

    p_State->Settings.Version = SETTINGS_VERSION;
    SettingsManager_InitDefaultDisplay(&p_State->Settings);
    SettingsManager_InitDefaultProvisioning(&p_State->Settings);
    SettingsManager_InitDefaultWiFi(&p_State->Settings);
    SettingsManager_InitDefaultSystem(&p_State->Settings);
    SettingsManager_InitDefaultLepton(&p_State->Settings);
    SettingsManager_InitDefaultHTTPServer(&p_State->Settings);
    SettingsManager_InitDefaultVISAServer(&p_State->Settings);
    SettingsManager_InitDefaultLEDFlash(&p_State->Settings);
}