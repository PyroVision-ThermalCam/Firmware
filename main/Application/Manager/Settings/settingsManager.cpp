/*
 * settingsManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings Manager implementation.
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

#include <nvs_flash.h>
#include <nvs.h>

#include <string.h>

#include "settingsManager.h"
#include "../AppDiag/appDiag.h"
#include "Private/settingsLoader.h"

static const char *TAG = "Settings-Manager";

ESP_EVENT_DEFINE_BASE(SETTINGS_EVENTS);

static Settings_Manager_State_t _SettingsManagerState;

/** @brief                  Update a specific settings section in the Settings Manager RAM and emit the corresponding event.
 *  @param p_Src            Pointer to source settings structure
 *  @param p_Dst            Pointer to destination settings structure in RAM
 *  @param Size             Size of the settings structure to copy
 *  @param EventID          Event identifier to emit after update
 *  @param p_ChangedSetting Pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success
 */
static esp_err_t SettingsManager_Update(void *p_Src, void *p_Dst, size_t Size, int EventID,
                                        SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL)
{
    if (_SettingsManagerState.IsInitialized == false) {
        return SETTINGS_ERR_NOT_INITIALIZED;
    } else if ((p_Src == NULL) || (p_Dst == NULL) || (Size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);

    __builtin_memcpy(p_Dst, p_Src, Size);

    xSemaphoreGive(_SettingsManagerState.Mutex);

    /* Only include event data if p_ChangedSetting is valid */
    esp_event_post(SETTINGS_EVENTS, EventID, p_ChangedSetting,
                   p_ChangedSetting ? sizeof(SettingsManager_ChangeNotification_t) : 0, pdMS_TO_TICKS(100));

    return ESP_OK;
}

/** @brief                  Get a specific settings section from the Settings Manager RAM.
 *  @param p_Output         Pointer to output structure to populate with settings data
 *  @param p_Source         Pointer to source settings structure in RAM
 *  @param Size             Size of the settings structure to copy
 * @return                  ESP_OK on success, ESP_ERR_* on failure
 */
static esp_err_t SettingsManager_Get(void* p_Output, void* p_Source, size_t Size)
{
    if ( p_Output == NULL ) {
        return ESP_ERR_INVALID_ARG;
    } else if (_SettingsManagerState.IsInitialized == false) {
        return SETTINGS_ERR_NOT_INITIALIZED;
    }

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);
    __builtin_memcpy(p_Output, p_Source, Size);
    xSemaphoreGive(_SettingsManagerState.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_Init(void)
{
    uint16_t Serial;
    esp_err_t Error;

    if (_SettingsManagerState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Settings Manager...");

    __builtin_memset(&_SettingsManagerState, 0, sizeof(Settings_Manager_State_t));

    ESP_ERROR_CHECK(nvs_flash_init());

    Error = nvs_flash_init_partition("settings");
    if ((Error == ESP_ERR_NVS_NO_FREE_PAGES) || (Error == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "Settings partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase_partition("settings"));

        nvs_flash_init_partition("settings");
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize settings partition: 0x%X!", Error);

        return Error;
    }

    _SettingsManagerState.Mutex = xSemaphoreCreateMutex();
    if (_SettingsManagerState.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");

        return ESP_ERR_NO_MEM;
    }

    Error = nvs_open_from_partition("settings", CONFIG_SETTINGS_NAMESPACE, NVS_READWRITE,
                                    &_SettingsManagerState.NVSHandle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: 0x%X!", Error);

        vSemaphoreDelete(_SettingsManagerState.Mutex);

        return Error;
    }

    _SettingsManagerState.IsInitialized = true;

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);

    /* Get the serial from NVS. Use a temporary variable to prevent alignment errors. */
    Error = nvs_get_u16(_SettingsManagerState.NVSHandle, "serial", &Serial);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get serial number from NVS: 0x%X!. Using 0", Error);

        Serial = 0;
    }

    xSemaphoreGive(_SettingsManagerState.Mutex);

    /* Copy the read-only data */
    snprintf(_SettingsManagerState.Info.FirmwareVersion, sizeof(_SettingsManagerState.Info.FirmwareVersion), "%u.%u.%u", PYROVISION_VERSION_MAJOR, PYROVISION_VERSION_MINOR,
            PYROVISION_VERSION_BUILD);
    snprintf(_SettingsManagerState.Info.Manufacturer, sizeof(_SettingsManagerState.Info.Manufacturer), "%s", CONFIG_DEVICE_MANUFACTURER);
    snprintf(_SettingsManagerState.Info.Name, sizeof(_SettingsManagerState.Info.Name), "%s", CONFIG_DEVICE_NAME);
    snprintf(_SettingsManagerState.Info.Serial, sizeof(_SettingsManagerState.Info.Serial), "%u", Serial);
    snprintf(_SettingsManagerState.Info.SDK, sizeof(_SettingsManagerState.Info.SDK), "%s", IDF_VER);

    /* Get application description with version info */
    const esp_app_desc_t *p_AppDesc = esp_app_get_description();
    snprintf(_SettingsManagerState.Info.Commit, sizeof(_SettingsManagerState.Info.Commit), "%s", p_AppDesc->version);
    ESP_LOGI(TAG, "Firmware Version: %s", p_AppDesc->version);
    ESP_LOGI(TAG, "Firmware Date: %s %s", p_AppDesc->date, p_AppDesc->time);
    ESP_LOGI(TAG, "Firmware IDF: %s", p_AppDesc->idf_ver);
    ESP_LOGI(TAG, "Manufacturer: %s", _SettingsManagerState.Info.Manufacturer);
    ESP_LOGI(TAG, "Device Name: %s", _SettingsManagerState.Info.Name);
    ESP_LOGI(TAG, "Serial: %s", _SettingsManagerState.Info.Serial);

    /* Read the bootloader information */
    const esp_partition_t *p_BootloaderPartition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                                            ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                                            NULL);
    if (p_BootloaderPartition != NULL) {
        Error = esp_ota_get_partition_description(p_BootloaderPartition, &_SettingsManagerState.Info.Bootloader);
        if (Error == ESP_OK) {
            ESP_LOGD(TAG, "Bootloader version: %s", _SettingsManagerState.Info.Bootloader.version);
            ESP_LOGD(TAG, "Bootloader date: %s %s", _SettingsManagerState.Info.Bootloader.date,
                     _SettingsManagerState.Info.Bootloader.time);
            ESP_LOGD(TAG, "Bootloader IDF version: %s", _SettingsManagerState.Info.Bootloader.idf_ver);
        } else {
            ESP_LOGW(TAG, "Failed to read bootloader description: 0x%X!", Error);
        }
    } else {
        ESP_LOGW(TAG, "Bootloader partition not found");
    }

    /* Load the settings from the NVS */
    Error = SettingsManager_LoadFromNVS(&_SettingsManagerState.Settings);
    if (Error != ESP_OK) {
        ESP_LOGI(TAG, "No settings found, using JSON config defaults");

        /* Try to load default settings from JSON first (on first boot) */
        if (SettingsManager_LoadFromJSON(&_SettingsManagerState, "/storage/settings.json") != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load default settings from JSON, using built-in defaults");

            /* Use built-in defaults */
            SettingsManager_LoadFromDefaults(&_SettingsManagerState);
        }

        /* Save the default settings to NVS */
        SettingsManager_Save();

        /* Load the JSON presets into the settings structure */
        SettingsManager_LoadFromNVS(&_SettingsManagerState.Settings);
    }

    ESP_LOGI(TAG, "Settings Manager initialized");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_LOADED, &_SettingsManagerState.Settings, sizeof(Settings_t),
                   pdMS_TO_TICKS(100));

    return ESP_OK;
}

esp_err_t SettingsManager_Deinit(void)
{
    if (_SettingsManagerState.IsInitialized == false) {
        return ESP_OK;
    }

    nvs_close(_SettingsManagerState.NVSHandle);
    vSemaphoreDelete(_SettingsManagerState.Mutex);

    _SettingsManagerState.IsInitialized = false;

    ESP_LOGI(TAG, "Settings Manager deinitialized");

    return ESP_OK;
}

esp_err_t SettingsManager_LoadFromNVS(Settings_t *p_Settings)
{
    esp_err_t Error;
    size_t RequiredSize;
    uint8_t ConfigValid;

    if (_SettingsManagerState.IsInitialized == false) {
        return SETTINGS_ERR_NOT_INITIALIZED;
    } else if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);

    /* Check if the config is valid */
    Error = nvs_get_u8(_SettingsManagerState.NVSHandle, "config_valid", &ConfigValid);
    if ((Error != ESP_OK) || (ConfigValid != 1)) {
        ESP_LOGE(TAG, "Failed to read config_valid flag: 0x%X!", Error);

        Error = SETTINGS_ERR_NVS_NOT_FOUND;

        goto SettingsManager_LoadFromNVS_Exit;
    }

    /* Get the settings version from NVS. Continue loading if the version numbers match. */
    Error = nvs_get_u32(_SettingsManagerState.NVSHandle, "version", &p_Settings->Version);
    if ((Error == ESP_OK) && (p_Settings->Version == SETTINGS_VERSION)) {
        Error = nvs_get_blob(_SettingsManagerState.NVSHandle, "settings", NULL, &RequiredSize);
        if (Error == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Settings not found in NVS!");

            Error = SETTINGS_ERR_NVS_NOT_FOUND;

            goto SettingsManager_LoadFromNVS_Exit;
        } else if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get settings size: 0x%X!", Error);

            xSemaphoreGive(_SettingsManagerState.Mutex);

            goto SettingsManager_LoadFromNVS_Exit;
        }

        if (RequiredSize != sizeof(Settings_t)) {
            ESP_LOGW(TAG, "Settings size mismatch (expected %u, got %u), erasing and using defaults",
                     sizeof(Settings_t), RequiredSize);

            /* Erase the old settings */
            nvs_erase_key(_SettingsManagerState.NVSHandle, "settings");
            nvs_commit(_SettingsManagerState.NVSHandle);

            Error = SETTINGS_ERR_SIZE_MISMATCH;

            goto SettingsManager_LoadFromNVS_Exit;
        }

        Error = nvs_get_blob(_SettingsManagerState.NVSHandle, "settings", &_SettingsManagerState.Settings, &RequiredSize);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read settings: 0x%X!", Error);

            goto SettingsManager_LoadFromNVS_Exit;
        }

        __builtin_memcpy(p_Settings, &_SettingsManagerState.Settings, sizeof(Settings_t));

        ESP_LOGD(TAG, "Settings loaded from NVS");

        Error = ESP_OK;
    }
    /* We reach this case when we can no read a settings version because it does not exist or does not match */
    else {
        ESP_LOGI(TAG, "Settings version mismatch or not found in NVS (expected %u, got %u)",
                 SETTINGS_VERSION, p_Settings->Version);

        Error = SETTINGS_ERR_VERSION_MISMATCH;
    }

SettingsManager_LoadFromNVS_Exit:
    xSemaphoreGive(_SettingsManagerState.Mutex);

    return Error;
}

esp_err_t SettingsManager_Save(void)
{
    esp_err_t Error;

    if (_SettingsManagerState.IsInitialized == false) {
        return SETTINGS_ERR_NOT_INITIALIZED;
    }

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);

    /* Save the version number first */
    Error = nvs_set_u32(_SettingsManagerState.NVSHandle, "version", _SettingsManagerState.Settings.Version);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write version: 0x%X!", Error);

        goto SettingsManager_Save_Error;
    }

    Error = nvs_set_blob(_SettingsManagerState.NVSHandle, "settings", &_SettingsManagerState.Settings,
                         sizeof(Settings_t));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write settings: 0x%X!", Error);

        goto SettingsManager_Save_Error;
    }

    /* Mark config as valid */
    Error = nvs_set_u8(_SettingsManagerState.NVSHandle, "config_valid", true);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_valid flag: 0x%X!", Error);

        goto SettingsManager_Save_Error;
    }

    Error = nvs_commit(_SettingsManagerState.NVSHandle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: 0x%X!", Error);

        goto SettingsManager_Save_Error;
    }

    xSemaphoreGive(_SettingsManagerState.Mutex);

    ESP_LOGI(TAG, "Settings saved to NVS (version %u)", _SettingsManagerState.Settings.Version);

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_SAVED, NULL, 0, pdMS_TO_TICKS(100));

    return ESP_OK;

SettingsManager_Save_Error:
    xSemaphoreGive(_SettingsManagerState.Mutex);

    APP_DIAG_RECORD(APP_DIAG_SOURCE_SETTINGS, SETTINGS_ERR_NVS_WRITE);

    ESP_LOGE(TAG, "Failed to save settings to NVS: 0x%X!", Error);

    return SETTINGS_ERR_NVS_WRITE;
}

esp_err_t SettingsManager_GetInfo(Settings_Info_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Info, sizeof(Settings_Info_t));
}

esp_err_t SettingsManager_GetLepton(Settings_Lepton_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.Lepton, sizeof(Settings_Lepton_t));
}

esp_err_t SettingsManager_UpdateLepton(Settings_Lepton_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.Lepton, sizeof(Settings_Lepton_t),
                                  SETTINGS_EVENT_LEPTON_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetWiFi(Settings_WiFi_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.WiFi, sizeof(Settings_WiFi_t));
}

esp_err_t SettingsManager_UpdateWiFi(Settings_WiFi_t *p_Settings,
                                     SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.WiFi, sizeof(Settings_WiFi_t),
                                  SETTINGS_EVENT_WIFI_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetProvisioning(Settings_Provisioning_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.Provisioning, sizeof(Settings_Provisioning_t));
}

esp_err_t SettingsManager_UpdateProvisioning(Settings_Provisioning_t *p_Settings,
                                             SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.Provisioning,
                                  sizeof(Settings_Provisioning_t),
                                  SETTINGS_EVENT_PROVISIONING_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetDisplay(Settings_Display_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.Display, sizeof(Settings_Display_t));
}

esp_err_t SettingsManager_UpdateDisplay(Settings_Display_t *p_Settings,
                                        SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.Display, sizeof(Settings_Display_t),
                                  SETTINGS_EVENT_DISPLAY_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetHTTPServer(Settings_HTTP_Server_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.HTTPServer, sizeof(Settings_HTTP_Server_t));
}

esp_err_t SettingsManager_UpdateHTTPServer(Settings_HTTP_Server_t *p_Settings,
                                           SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.HTTPServer,
                                  sizeof(Settings_HTTP_Server_t),
                                  SETTINGS_EVENT_HTTP_SERVER_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetVISAServer(Settings_VISA_Server_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.VISAServer, sizeof(Settings_VISA_Server_t));
}

esp_err_t SettingsManager_UpdateVISAServer(Settings_VISA_Server_t *p_Settings,
                                           SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.VISAServer,
                                  sizeof(Settings_VISA_Server_t),
                                  SETTINGS_EVENT_VISA_SERVER_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetSystem(Settings_System_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.System, sizeof(Settings_System_t));
}

esp_err_t SettingsManager_UpdateSystem(Settings_System_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.System, sizeof(Settings_System_t),
                                  SETTINGS_EVENT_SYSTEM_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetLEDFlash(Settings_LED_Flash_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.LEDFlash, sizeof(Settings_LED_Flash_t));
}

esp_err_t SettingsManager_UpdateLEDFlash(Settings_LED_Flash_t *p_Settings,
                                         SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.LEDFlash, sizeof(Settings_LED_Flash_t),
                                  SETTINGS_EVENT_LED_FLASH_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetUSB(Settings_USB_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.USB, sizeof(Settings_USB_t));
}

esp_err_t SettingsManager_UpdateUSB(Settings_USB_t *p_Settings, SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.USB, sizeof(Settings_USB_t),
                                  SETTINGS_EVENT_USB_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetCalibration(Settings_Calibration_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.Calibration,
                               sizeof(Settings_Calibration_t));
}

esp_err_t SettingsManager_UpdateCalibration(Settings_Calibration_t *p_Settings,
                                            SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.Calibration,
                                  sizeof(Settings_Calibration_t),
                                  SETTINGS_EVENT_CALIBRATION_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_GetImage(Settings_Image_t *p_Settings)
{
    return SettingsManager_Get(p_Settings, &_SettingsManagerState.Settings.Image, sizeof(Settings_Image_t));
}

esp_err_t SettingsManager_UpdateImage(Settings_Image_t *p_Settings,
                                      SettingsManager_ChangeNotification_t *p_ChangedSetting)
{
    return SettingsManager_Update(p_Settings, &_SettingsManagerState.Settings.Image,
                                  sizeof(Settings_Image_t),
                                  SETTINGS_EVENT_IMAGE_CHANGED, p_ChangedSetting);
}

esp_err_t SettingsManager_ResetToDefaults(void)
{
    esp_err_t Error;

    if (_SettingsManagerState.IsInitialized == false) {
        return SETTINGS_ERR_NOT_INITIALIZED;
    }

    ESP_LOGW(TAG, "Resetting settings to factory defaults");

    xSemaphoreTake(_SettingsManagerState.Mutex, portMAX_DELAY);

    Error = nvs_erase_key(_SettingsManagerState.NVSHandle, "settings");
    if (Error != ESP_OK && Error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: 0x%X!", Error);

        xSemaphoreGive(_SettingsManagerState.Mutex);

        return Error;
    }

    Error = nvs_commit(_SettingsManagerState.NVSHandle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: 0x%X!", Error);

        xSemaphoreGive(_SettingsManagerState.Mutex);

        return Error;
    }

    /* Reset config_valid flag to allow reloading default config */
    Error = nvs_set_u8(_SettingsManagerState.NVSHandle, "config_valid", false);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_valid flag: 0x%X!", Error);

        xSemaphoreGive(_SettingsManagerState.Mutex);

        return Error;
    }

    xSemaphoreGive(_SettingsManagerState.Mutex);

    /* Reboot the ESP to allow reloading the settings config */
    esp_restart();

    /* Never reached */
    return ESP_OK;
}

