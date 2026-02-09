/*
 * settingsManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Persistent settings management using NVS storage.
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
#include <esp_mac.h>
#include <esp_efuse.h>
#include <esp_littlefs.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <string.h>
#include <sys/stat.h>
#include <cJSON.h>

#include "settingsManager.h"
#include "Private/settingsLoader.h"

static const char *TAG = "settings_manager";

ESP_EVENT_DEFINE_BASE(SETTINGS_EVENTS);

static SettingsManager_State_t _State;

/** @brief                  Update a specific settings section in the Settings Manager RAM and emit the corresponding event.
 *  @param p_Src            Pointer to source settings structure
 *  @param p_Dst            Pointer to destination settings structure in RAM
 *  @param Size             Size of the settings structure to copy
 *  @param EventID          Event identifier to emit after update
 *  @param p_ChangedSetting Pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success
 */
static esp_err_t SettingsManager_Update(void *p_Src, void *p_Dst, size_t Size, int EventID,
                                        SettingsManager_Setting_t *p_ChangedSetting = NULL)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if ((p_Src == NULL) || (p_Dst == NULL) || (Size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    memcpy(p_Dst, p_Src, Size);

    xSemaphoreGive(_State.Mutex);

    esp_event_post(SETTINGS_EVENTS, EventID, p_ChangedSetting, sizeof(SettingsManager_Setting_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_Init(void)
{
    uint16_t Serial;
    esp_err_t Error;

    if (_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Settings Manager...");

    ESP_ERROR_CHECK(nvs_flash_init());

    Error = nvs_flash_init_partition("settings");
    if ((Error == ESP_ERR_NVS_NO_FREE_PAGES) || (Error == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "Settings partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase_partition("settings"));
        Error = nvs_flash_init_partition("settings");
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize settings partition: %d!", Error);
        return Error;
    }

    _State.Mutex = xSemaphoreCreateMutex();
    if (_State.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");

        return ESP_ERR_NO_MEM;
    }

    Error = nvs_open_from_partition("settings", CONFIG_SETTINGS_NAMESPACE, NVS_READWRITE, &_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %d!", Error);

        vSemaphoreDelete(_State.Mutex);

        return Error;
    }

    _State.isInitialized = true;

    /* Copy the read-only data */
    sprintf(_State.Info.FirmwareVersion, "%u.%u.%u", PYROVISION_VERSION_MAJOR, PYROVISION_VERSION_MINOR,
            PYROVISION_VERSION_BUILD);
    sprintf(_State.Info.Manufacturer, "%s", CONFIG_DEVICE_MANUFACTURER);

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    /* Get the serial from NVS. Use a temporary variable to prevent alignment errors. */
    Error = nvs_get_u16(_State.NVS_Handle, "serial", &Serial);
    _State.Info.Serial = Serial;

    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get serial number from NVS: %d!. Using 0", Error);
        _State.Info.Serial = 0;
    }
    xSemaphoreGive(_State.Mutex);

    /* Load the settings from the NVS */
    Error = SettingsManager_Load(&_State.Settings);
    if (Error != ESP_OK) {
        ESP_LOGI(TAG, "No settings found, using factory defaults");

        /* Try to load default settings from JSON first (on first boot) */
        if (SettingsManager_LoadDefaultsFromJSON(&_State) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load default settings from JSON, using built-in defaults");

            /* Use built-in defaults */
            SettingsManager_InitDefaults(&_State);
        }

        /* Save the default settings to NVS */
        SettingsManager_Save();

        /* Load the JSON presets into the settings structure */
        SettingsManager_Load(&_State.Settings);
    }

    ESP_LOGI(TAG, "Settings Manager initialized");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_LOADED, &_State.Settings, sizeof(App_Settings_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_Deinit(void)
{
    if (_State.isInitialized == false) {
        return ESP_OK;
    }

    nvs_close(_State.NVS_Handle);
    vSemaphoreDelete(_State.Mutex);

    _State.isInitialized = false;

    ESP_LOGI(TAG, "Settings Manager deinitialized");

    return ESP_OK;
}

esp_err_t SettingsManager_Load(App_Settings_t *p_Settings)
{
    esp_err_t Error;
    size_t RequiredSize;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_get_blob(_State.NVS_Handle, "settings", NULL, &RequiredSize);
    if (Error == ESP_ERR_NVS_NOT_FOUND) {
        xSemaphoreGive(_State.Mutex);

        return ESP_ERR_NVS_NOT_FOUND;
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get settings size: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    if (RequiredSize != sizeof(App_Settings_t)) {
        ESP_LOGW(TAG, "Settings size mismatch (expected %u, got %u), erasing and using defaults",
                 sizeof(App_Settings_t), RequiredSize);

        /* Erase the old settings */
        nvs_erase_key(_State.NVS_Handle, "settings");
        nvs_commit(_State.NVS_Handle);

        xSemaphoreGive(_State.Mutex);

        return ESP_ERR_INVALID_SIZE;
    }

    Error = nvs_get_blob(_State.NVS_Handle, "settings", &_State.Settings, &RequiredSize);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);

        return Error;
    }

    memcpy(p_Settings, &_State.Settings, sizeof(App_Settings_t));

    xSemaphoreGive(_State.Mutex);

    ESP_LOGD(TAG, "Settings loaded from NVS");

    return ESP_OK;
}

esp_err_t SettingsManager_Save(void)
{
    esp_err_t Error;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_set_blob(_State.NVS_Handle, "settings", &_State.Settings, sizeof(App_Settings_t));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    Error = nvs_commit(_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Settings saved to NVS");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_SAVED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_GetInfo(App_Settings_Info_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Info, sizeof(App_Settings_Info_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_GetLepton(App_Settings_Lepton_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.Lepton, sizeof(App_Settings_Lepton_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateLepton(App_Settings_Lepton_t *p_Settings, SettingsManager_Setting_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.Lepton, sizeof(App_Settings_Lepton_t),
                                  SETTINGS_EVENT_LEPTON_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetWiFi(App_Settings_WiFi_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.WiFi, sizeof(App_Settings_WiFi_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateWiFi(App_Settings_WiFi_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.WiFi, sizeof(App_Settings_WiFi_t),
                                  SETTINGS_EVENT_WIFI_CHANGED);
}

esp_err_t SettingsManager_GetProvisioning(App_Settings_Provisioning_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.Provisioning, sizeof(App_Settings_Provisioning_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateProvisioning(App_Settings_Provisioning_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.Provisioning, sizeof(App_Settings_Provisioning_t),
                                  SETTINGS_EVENT_PROVISIONING_CHANGED);
}

esp_err_t SettingsManager_GetDisplay(App_Settings_Display_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.Display, sizeof(App_Settings_Display_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateDisplay(App_Settings_Display_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.Display, sizeof(App_Settings_Display_t),
                                  SETTINGS_EVENT_DISPLAY_CHANGED);
}

esp_err_t SettingsManager_GetHTTPServer(App_Settings_HTTP_Server_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.HTTPServer, sizeof(App_Settings_HTTP_Server_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateHTTPServer(App_Settings_HTTP_Server_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.HTTPServer, sizeof(App_Settings_HTTP_Server_t),
                                  SETTINGS_EVENT_HTTP_SERVER_CHANGED);
}

esp_err_t SettingsManager_GetVISAServer(App_Settings_VISA_Server_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.VISAServer, sizeof(App_Settings_VISA_Server_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateVISAServer(App_Settings_VISA_Server_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.VISAServer, sizeof(App_Settings_VISA_Server_t),
                                  SETTINGS_EVENT_VISA_SERVER_CHANGED);
}

esp_err_t SettingsManager_GetSystem(App_Settings_System_t *p_Settings)
{
    if ( p_Settings == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings.System, sizeof(App_Settings_System_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_UpdateSystem(App_Settings_System_t *p_Settings)
{
    return SettingsManager_Update(p_Settings, &_State.Settings.System, sizeof(App_Settings_System_t),
                                  SETTINGS_EVENT_SYSTEM_CHANGED);
}

esp_err_t SettingsManager_ResetToDefaults(void)
{
    esp_err_t Error;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Resetting settings to factory defaults");

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_erase_key(_State.NVS_Handle, "settings");
    if (Error != ESP_OK && Error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    Error = nvs_commit(_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    /* Reset config_loaded flag to allow reloading default config */
    Error = nvs_set_u8(_State.NVS_Handle, "config_loaded", false);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_loaded flag: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    xSemaphoreGive(_State.Mutex);

    /* Reboot the ESP to allow reloading the settings config */
    esp_restart();

    /* Never reached */
    return ESP_OK;
}

