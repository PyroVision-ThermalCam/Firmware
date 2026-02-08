/*
 * settingsJSONLoader.cpp
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

#include <esp_log.h>
#include <esp_littlefs.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>

#include <string.h>
#include <sys/stat.h>
#include <cJSON.h>
#include <sys/stat.h>
#include <dirent.h>

#include "settingsLoader.h"
#include "../settingsManager.h"

static const char *TAG = "settings_json_loader";

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadLepton(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *lepton = NULL;
    cJSON *emissivity_array = NULL;
    cJSON *roi_array = NULL;

    lepton = cJSON_GetObjectItem(p_JSON, "lepton");
    if (lepton != NULL) {
        emissivity_array = cJSON_GetObjectItem(lepton, "emissivity");
        if (cJSON_IsArray(emissivity_array)) {
            p_State->Settings.Lepton.EmissivityCount = cJSON_GetArraySize(emissivity_array);

            ESP_LOGD(TAG, "Found %d emissivity presets in JSON", p_State->Settings.Lepton.EmissivityCount);

            for (uint32_t i = 0; i < p_State->Settings.Lepton.EmissivityCount; i++) {
                cJSON *preset = cJSON_GetArrayItem(emissivity_array, i);
                cJSON *name = cJSON_GetObjectItem(preset, "name");
                cJSON *value = cJSON_GetObjectItem(preset, "value");

                if (cJSON_IsString(name) && cJSON_IsNumber(value)) {
                    p_State->Settings.Lepton.EmissivityPresets[i].Value = (float)(value->valuedouble);

                    /* Cap the emissivity value between 0 and 100 */
                    if (p_State->Settings.Lepton.EmissivityPresets[i].Value < 0.0f) {
                        p_State->Settings.Lepton.EmissivityPresets[i].Value = 0.0f;
                    } else if (p_State->Settings.Lepton.EmissivityPresets[i].Value > 100.0f) {
                        p_State->Settings.Lepton.EmissivityPresets[i].Value = 100.0f;
                    }

                    memset(p_State->Settings.Lepton.EmissivityPresets[i].Description, 0,
                           sizeof(p_State->Settings.Lepton.EmissivityPresets[i].Description));
                    strncpy(p_State->Settings.Lepton.EmissivityPresets[i].Description, name->valuestring,
                            sizeof(p_State->Settings.Lepton.EmissivityPresets[i].Description));

                    ESP_LOGD(TAG, "  Preset %d: %s = %.2f", i, name->valuestring, value->valuedouble);
                }
            }

            p_State->Settings.Lepton.CurrentEmissivity = SETTINGS_DEFAULT_LEPTON_EMISSIVITY;
        } else {
            SettingsManager_InitDefaultLeptonEmissivityPresets(&p_State->Settings);
        }

        roi_array = cJSON_GetObjectItem(lepton, "roi");
        if (cJSON_IsArray(roi_array)) {
        } else {
            SettingsManager_InitDefaultLeptonROIs(&p_State->Settings);
        }
    } else {
        SettingsManager_InitDefaultLepton(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadDisplay(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *display = NULL;

    display = cJSON_GetObjectItem(p_JSON, "display");
    if (display != NULL) {
        cJSON *brightness = cJSON_GetObjectItem(display, "brightness");
        if (cJSON_IsNumber(brightness)) {
            p_State->Settings.Display.Brightness = (uint8_t)(brightness->valueint);
        } else {
            p_State->Settings.Display.Brightness = SETTINGS_DISPLAY_DEFAULT_BRIGHTNESS;
        }

        cJSON *timeout = cJSON_GetObjectItem(display, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.Display.Timeout = (uint16_t)(timeout->valueint);
        } else {
            p_State->Settings.Display.Timeout = SETTINGS_DISPLAY_DEFAULT_TIMEOUT;
        }
    } else {
        SettingsManager_InitDefaultDisplay(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadWiFi(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *wifi = NULL;

    wifi = cJSON_GetObjectItem(p_JSON, "wifi");
    if (wifi != NULL) {
        cJSON *maxRetries = cJSON_GetObjectItem(wifi, "maxRetries");
        if (cJSON_IsNumber(maxRetries)) {
            p_State->Settings.WiFi.MaxRetries = (uint8_t)(maxRetries->valueint);
        } else {
            p_State->Settings.WiFi.MaxRetries = SETTINGS_WIFI_DEFAULT_MAX_RETRIES;
        }

        cJSON *retryInterval = cJSON_GetObjectItem(wifi, "retryInterval");
        if (cJSON_IsNumber(retryInterval)) {
            p_State->Settings.WiFi.RetryInterval = (uint32_t)(retryInterval->valueint);
        } else {
            p_State->Settings.WiFi.RetryInterval = SETTINGS_WIFI_DEFAULT_RETRY_INTERVAL;
        }

        cJSON *autoConnect = cJSON_GetObjectItem(wifi, "autoConnect");
        if (cJSON_IsBool(autoConnect)) {
            p_State->Settings.WiFi.AutoConnect = cJSON_IsTrue(autoConnect);
        } else {
            p_State->Settings.WiFi.AutoConnect = SETTINGS_WIFI_DEFAULT_AUTOCONNECT;
        }

        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        if (cJSON_IsString(ssid)) {
            strncpy(p_State->Settings.WiFi.SSID, ssid->valuestring, sizeof(p_State->Settings.WiFi.SSID));
        } else {
            strncpy(p_State->Settings.WiFi.SSID, SETTINGS_WIFI_DEFAULT_SSID, sizeof(p_State->Settings.WiFi.SSID));
        }

        cJSON *password = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(password)) {
            strncpy(p_State->Settings.WiFi.Password, password->valuestring, sizeof(p_State->Settings.WiFi.Password));
        } else {
            strncpy(p_State->Settings.WiFi.Password, SETTINGS_WIFI_DEFAULT_PASSWORD, sizeof(p_State->Settings.WiFi.Password));
        }
    } else {
        SettingsManager_InitDefaultWiFi(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadProvisioning(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *provisioning = NULL;

    provisioning = cJSON_GetObjectItem(p_JSON, "provisioning");
    if (provisioning != NULL) {
        cJSON *name = cJSON_GetObjectItem(provisioning, "name");
        if (cJSON_IsString(name)) {
            strncpy(p_State->Settings.Provisioning.Name, name->valuestring, sizeof(p_State->Settings.Provisioning.Name));
        } else {
            strncpy(p_State->Settings.Provisioning.Name, SETTINGS_PROVISIONING_DEFAULT_NAME,
                    sizeof(p_State->Settings.Provisioning.Name));
        }

        cJSON *timeout = cJSON_GetObjectItem(provisioning, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.Provisioning.Timeout = (uint32_t)(timeout->valueint);
        } else {
            p_State->Settings.Provisioning.Timeout = SETTINGS_PROVISIONING_DEFAULT_TIMEOUT;
        }
    } else {
        SettingsManager_InitDefaultProvisioning(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadSystem(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *system = NULL;

    system = cJSON_GetObjectItem(p_JSON, "system");
    if (system != NULL) {
        cJSON *timezone = cJSON_GetObjectItem(system, "timezone");
        if (cJSON_IsString(timezone)) {
            strncpy(p_State->Settings.System.Timezone, timezone->valuestring, sizeof(p_State->Settings.System.Timezone));
        } else {
            strncpy(p_State->Settings.System.Timezone, SETTINGS_SYSTEM_DEFAULT_TIMEZONE, sizeof(p_State->Settings.System.Timezone));
        }

        cJSON *devicename = cJSON_GetObjectItem(system, "devicename");
        if (cJSON_IsString(devicename)) {
            strncpy(p_State->Settings.System.DeviceName, devicename->valuestring, sizeof(p_State->Settings.System.DeviceName));
        } else {
            strncpy(p_State->Settings.System.DeviceName, SETTINGS_SYSTEM_DEFAULT_DEVICENAME,
                    sizeof(p_State->Settings.System.DeviceName));
        }
    } else {
        SettingsManager_InitDefaultSystem(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadHTTPServer(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *http_server = NULL;

    http_server = cJSON_GetObjectItem(p_JSON, "http-server");
    if (http_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(http_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.HTTPServer.Port = (uint16_t)(port->valueint);
        } else {
            p_State->Settings.HTTPServer.Port = SETTINGS_DEFAULT_HTTP_PORT;
        }

        cJSON *wsPingIntervalSec = cJSON_GetObjectItem(http_server, "wsPingIntervalSec");
        if (cJSON_IsNumber(wsPingIntervalSec)) {
            p_State->Settings.HTTPServer.WSPingIntervalSec = (uint16_t)(wsPingIntervalSec->valueint);
        } else {
            p_State->Settings.HTTPServer.WSPingIntervalSec = SETTINGS_DEFAULT_WS_PING_INTERVAL;
        }

        cJSON *maxClients = cJSON_GetObjectItem(http_server, "maxClients");
        if (cJSON_IsNumber(maxClients)) {
            p_State->Settings.HTTPServer.MaxClients = (uint8_t)(maxClients->valueint);
        } else {
            p_State->Settings.HTTPServer.MaxClients = SETTINGS_DEFAULT_HTTP_MAX_CLIENTS;
        }
    } else {
        SettingsManager_InitDefaultHTTPServer(&p_State->Settings);
    }
}

/** @brief
 *  @param p_State
 *  @param p_JSON
 */
static void SettingsManager_LoadVISAServer(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *visa_server = NULL;

    visa_server = cJSON_GetObjectItem(p_JSON, "visa-server");
    if (visa_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(visa_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.VISAServer.Port = (uint16_t)(port->valueint);
        } else {
            p_State->Settings.VISAServer.Port = SETTINGS_DEFAULT_VISA_PORT;
        }
    } else {
        SettingsManager_InitDefaultVISAServer(&p_State->Settings);
    }
}

/** @brief  Initialize and mount the SD card.
 *  @return ESP_OK on success
 */
static esp_err_t SettingsManager_Mount_SD_Card(void)
{
    esp_err_t Error;
    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";
    
    ESP_LOGD(TAG, "Initializing SD card");

    sdmmc_host_t Host = SDSPI_HOST_DEFAULT();
    Host.slot = SPI2_HOST;

    spi_bus_config_t SPI_Config;
    memset(&SPI_Config, 0, sizeof(SPI_Config));
    SPI_Config = {
        .mosi_io_num = GPIO_NUM_38,
        .miso_io_num = GPIO_NUM_40,
        .sclk_io_num = GPIO_NUM_39,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    Error = spi_bus_initialize(static_cast<spi_host_device_t>(Host.slot), &SPI_Config, SDSPI_DEFAULT_DMA);
    if (Error != ESP_OK) {
        if (Error == ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "SPI bus already initialized, continuing...");
        } else {
            ESP_LOGD(TAG, "Failed to initialize SPI: %d!", Error);

            return Error;
        }
    }

    sdspi_device_config_t Slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    Slot.gpio_cs = GPIO_NUM_47;
    Slot.host_id = static_cast<spi_host_device_t>(Host.slot);

    esp_vfs_fat_sdmmc_mount_config_t Mount_Config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    Error = esp_vfs_fat_sdspi_mount(mount_point, &Host, &Slot, &Mount_Config, &card); 
    if (Error != ESP_OK) {
        if (Error == ESP_FAIL) {
            ESP_LOGD(TAG, "Failed to mount SD card filesystem!");
        } else {
            ESP_LOGD(TAG, "Failed to mount SD card: 0x%x!", Error);
        }
        
        return Error;
    }

    /* Log the mount point and list files in the directory */
    DIR *dir = opendir(mount_point);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open mount point: %s!", mount_point);
        return ESP_FAIL;
    }

    struct dirent *entry;
    ESP_LOGD(TAG, "Files in mount point %s:", mount_point);
    /* Enhanced logging for file names */
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGD(TAG, "  Found file: %s", entry->d_name);
        if (strcmp(entry->d_name, "settings.json") == 0 || strcmp(entry->d_name, "SETTIN~1.JSO") == 0) {
            ESP_LOGD(TAG, "  Matched settings file: %s", entry->d_name);
        }
    }
    closedir(dir);

    ESP_LOGD(TAG, "SD card mounted successfully");

    return ESP_OK;
}

/** @brief Unmount the SD card.
 */
static void SettingsManager_Unmount_SD_Card(void)
{
    esp_vfs_fat_sdcard_unmount("/sdcard", NULL);
    ESP_LOGD(TAG, "SD card unmounted");
}

/** @brief          Load and parse JSON settings from file.
 *  @param p_State  Settings state structure
 *  @param filepath Full path to JSON file
 *  @return         ESP_OK on success
 */
static esp_err_t SettingsManager_Load_JSON(SettingsManager_State_t *p_State, const char *p_FilePath)
{
    FILE *File = NULL;
    char *Buffer = NULL;
    long FileSize;
    size_t BytesRead;
    cJSON *JSON = NULL;

    ESP_LOGD(TAG, "Loading settings from: %s", p_FilePath);

    /* Check if file exists */
    struct stat st;
    if (stat(p_FilePath, &st) != 0) {
        ESP_LOGE(TAG, "File does not exist or cannot be accessed: %s!", p_FilePath);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "Opening settings file...");
    File = fopen(p_FilePath, "r");
    if (File == NULL) {
        ESP_LOGW(TAG, "Failed to open: %s!", p_FilePath);

        return ESP_ERR_NOT_FOUND;
    }

    /* Get file size */
    fseek(File, 0, SEEK_END);
    FileSize = ftell(File);
    fseek(File, 0, SEEK_SET);

    if (FileSize <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld!", FileSize);

        fclose(File);

        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGD(TAG, "File size: %ld bytes", FileSize);

    /* Allocate buffer for file content */
    Buffer = (char*)heap_caps_malloc(FileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (Buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer!");

        fclose(File);

        return ESP_ERR_NO_MEM;
    }

    /* Read file content */
    BytesRead = fread(Buffer, 1, FileSize, File);
    fclose(File);
    Buffer[FileSize] = '\0';

    if (BytesRead != FileSize) {
        ESP_LOGE(TAG, "Read error: got %zu of %ld bytes!", BytesRead, FileSize);

        heap_caps_free(Buffer);

        return ESP_FAIL;
    }

    /* Parse JSON */
    JSON = cJSON_Parse(Buffer);
    heap_caps_free(Buffer);
    Buffer = NULL;

    if (JSON == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON Parse Error: %s!", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON Parse Error: Unknown!");
        }

        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "JSON parsed successfully from %s", p_FilePath);

    /* Load settings sections */
    SettingsManager_LoadDisplay(p_State, JSON);

    /* Extract provisioning settings */
    SettingsManager_LoadProvisioning(p_State, JSON);

    /* Extract WiFi settings */
    SettingsManager_LoadWiFi(p_State, JSON);

    /* Extract system settings */
    SettingsManager_LoadSystem(p_State, JSON);

    /* Extract Lepton settings */
    SettingsManager_LoadLepton(p_State, JSON);

    /* Extract HTTP Server settings */
    SettingsManager_LoadHTTPServer(p_State, JSON);

    /* Extract VISA Server settings */
    SettingsManager_LoadVISAServer(p_State, JSON);

    cJSON_Delete(JSON);

    return ESP_OK;
}

esp_err_t SettingsManager_LoadDefaultsFromJSON(SettingsManager_State_t *p_State)
{
    uint8_t ConfigLoaded = 0;
    esp_err_t Error;
    esp_vfs_littlefs_conf_t LittleFS_Config = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .partition = NULL,
		.format_if_mount_failed = true,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false
    };

    Error = nvs_get_u8(p_State->NVS_Handle, "config_loaded", &ConfigLoaded);
    if ((Error == ESP_OK) && (ConfigLoaded == true)) {
        ESP_LOGD(TAG, "Default config already loaded, skipping");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Loading settings with priority: SD Card -> LittleFS -> Built-in defaults");

    /* 1. Try SD card */
    Error = SettingsManager_Mount_SD_Card();
    if (Error == ESP_OK) {
        Error = SettingsManager_Load_JSON(p_State, "/sdcard/settings.json");

        /* Check for 8.3 filename match */
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Falling back to 8.3 filename: SETTIN~1.JSO");
            Error = SettingsManager_Load_JSON(p_State, "/sdcard/SETTIN~1.JSO");
        }

        if (Error == ESP_OK) {
            ESP_LOGD(TAG, "Settings loaded from SD card");
            
            /* Delete the settings file after successful load */
            if (remove("/sdcard/settings.json") == 0) {
                ESP_LOGD(TAG, "Deleted settings.json from SD card");
            } else if (remove("/sdcard/SETTIN~1.JSO") == 0) {
                ESP_LOGD(TAG, "Deleted SETTIN~1.JSO from SD card");
            }
            
            SettingsManager_Unmount_SD_Card();
            return ESP_OK;
        }
        
        SettingsManager_Unmount_SD_Card();
        ESP_LOGW(TAG, "SD card mounted but no valid settings.json found");
    } else {
        ESP_LOGD(TAG, "SD card not available, trying LittleFS");
    }

    /* 2. Try LittleFS */
    Error = esp_vfs_littlefs_register(&LittleFS_Config);
    if (Error == ESP_OK) {        
        Error = SettingsManager_Load_JSON(p_State, "/littlefs/settings.json");
        
        if (Error == ESP_OK) {
            ESP_LOGD(TAG, "Settings loaded from LittleFS");
            
            /* Delete the settings file after successful load */
            if (remove("/littlefs/settings.json") == 0) {
                ESP_LOGD(TAG, "Deleted settings.json from LittleFS");
            }
            
            esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);
            return ESP_OK;
        }
        
        esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);
        ESP_LOGW(TAG, "LittleFS mounted but no valid settings.json found");
    } else {
        ESP_LOGW(TAG, "Failed to mount LittleFS: %d!", Error);
    }

    /* 3. Fallback to built-in defaults */
    ESP_LOGW(TAG, "Using built-in default settings");
    SettingsManager_InitDefaults(p_State);

    /* Mark config as loaded */
    Error = nvs_set_u8(p_State->NVS_Handle, "config_loaded", true);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_loaded flag: %d!", Error);
        return Error;
    }

    Error = nvs_commit(p_State->NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config_loaded flag: %d!", Error);
        return Error;
    }

    ESP_LOGD(TAG, "Default config loaded and marked as valid");

    return ESP_OK;
}