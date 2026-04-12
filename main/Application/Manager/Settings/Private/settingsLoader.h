/*
 * settingsLoader.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: JSON settings loader for factory defaults.
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

#ifndef SETTINGS_LOADER_H_
#define SETTINGS_LOADER_H_

#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <stdbool.h>

#include "../settingsTypes.h"

#define SETTINGS_DEFAULT_LED_FLASH_ENABLE       true
#define SETTINGS_DEFAULT_LED_FLASH_POWER        100

#define SETTINGS_DEFAULT_USB_MSC_ENABLE         false
#define SETTINGS_DEFAULT_USB_UVC_ENABLE         false
#define SETTINGS_DEFAULT_USB_CDC_ENABLE         false
#define SETTINGS_DEFAULT_USB_UVC_ENABLE         false
#define SETTINGS_DEFAULT_USB_CDC_ENABLE         false

#define SETTINGS_DEFAULT_LEPTON_EMISSIVITY      100

#define SETTINGS_DEFAULT_VISA_PORT              5025
#define SETTINGS_DEFAULT_VISA_TIMEOUT_MS        5000

#define SETTINGS_DEFAULT_HTTP_PORT              80
#define SETTINGS_DEFAULT_WS_PING_INTERVAL       30
#define SETTINGS_DEFAULT_HTTP_MAX_CLIENTS       4
#define SETTINGS_DEFAULT_HTTP_ENABLE_CORS       false
#define SETTINGS_DEFAULT_HTTP_API_KEY           ""

#define SETTINGS_SYSTEM_DEFAULT_DEVICENAME      "PyroVision-Device"
#define SETTINGS_SYSTEM_DEFAULT_TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"
#define SETTINGS_SYSTEM_DEFAULT_NTP_SERVER      "pool.ntp.org"

#define SETTINGS_PROVISIONING_DEFAULT_TIMEOUT   300
#define SETTINGS_PROVISIONING_DEFAULT_NAME      "PyroVision-Provision"

#define SETTINGS_WIFI_DEFAULT_SSID              ""
#define SETTINGS_WIFI_DEFAULT_PASSWORD          ""
#define SETTINGS_WIFI_DEFAULT_MAX_RETRIES       5
#define SETTINGS_WIFI_DEFAULT_RETRY_INTERVAL    2000
#define SETTINGS_WIFI_DEFAULT_AUTOCONNECT       true

#define SETTINGS_DISPLAY_DEFAULT_BRIGHTNESS     80
#define SETTINGS_DISPLAY_DEFAULT_TIMEOUT        0

#define SETTINGS_DEFAULT_CALIBRATION_ROOM_TEMP  20

/** @brief Settings Manager state.
 */
typedef struct {
    bool isInitialized;
    nvs_handle_t NVS_Handle;
    Settings_t Settings;
    Settings_Info_t Info;
    SemaphoreHandle_t Mutex;
} Settings_Manager_State_t;


/** @brief          Load and parse JSON settings from file into RAM settings structure.
 *                  If the file is missing or invalid, returns an error and leaves settings unchanged.
 *  @param p_State  Settings state structure
 *  @param filepath Full path to JSON file
 *  @return         ESP_OK on success
 */
esp_err_t SettingsManager_LoadFromJSON(Settings_Manager_State_t *p_State, const char *p_FilePath);

/** @brief          Load factory default settings into RAM settings structure. This is used when no valid settings are found in NVS or JSON config.
 *  @param p_State  Pointer to Settings Manager state structure
 */
void SettingsManager_LoadFromDefaults(Settings_Manager_State_t *p_State);

/** @brief              Initialize Lepton ROIs with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultLeptonROIs(Settings_t *p_Settings);

/** @brief              Initialize Lepton emissivity presets with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultLeptonEmissivityPresets(Settings_t *p_Settings);

/** @brief              Initialize Display settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultDisplay(Settings_t *p_Settings);

/** @brief              Initialize Provisioning settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultProvisioning(Settings_t *p_Settings);

/** @brief              Initialize WiFi settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultWiFi(Settings_t *p_Settings);

/** @brief              Initialize System settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultSystem(Settings_t *p_Settings);

/** @brief              Initialize Lepton settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultLepton(Settings_t *p_Settings);

/** @brief              Initialize HTTP server settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultHTTPServer(Settings_t *p_Settings);

/** @brief              Initialize VISA server settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultVISAServer(Settings_t *p_Settings);

/** @brief              Initialize LED flash settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultLEDFlash(Settings_t *p_Settings);

/** @brief              Initialize USB settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultUSB(Settings_t *p_Settings);

/** @brief              Initialize Calibration settings with factory defaults.
 *  @param p_Settings   Pointer to settings structure
 */
void SettingsManager_InitDefaultCalibration(Settings_t *p_Settings);

#endif /* SETTINGS_LOADER_H_ */