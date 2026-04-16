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

static const char *TAG = "Settings-JSON-Loader";

/** @brief          Load the Lepton settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadLepton(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *lepton = NULL;
    cJSON *emissivity_array = NULL;
    cJSON *roi_array = NULL;

    lepton = cJSON_GetObjectItem(p_JSON, "lepton");
    if (lepton != NULL) {
        emissivity_array = cJSON_GetObjectItem(lepton, "emissivity");
        if (cJSON_IsArray(emissivity_array)) {
            p_State->Settings.Lepton.EmissivityPresetsCount = cJSON_GetArraySize(emissivity_array);

            ESP_LOGD(TAG, "Found %d emissivity presets in JSON", p_State->Settings.Lepton.EmissivityPresetsCount);

            for (uint32_t i = 0; i < p_State->Settings.Lepton.EmissivityPresetsCount; i++) {
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

/** @brief          Load the Display settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadDisplay(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *display = NULL;

    display = cJSON_GetObjectItem(p_JSON, "display");
    if (display != NULL) {
        cJSON *brightness = cJSON_GetObjectItem(display, "brightness");
        if (cJSON_IsNumber(brightness)) {
            p_State->Settings.Display.Brightness = static_cast<uint8_t>(brightness->valueint);
        } else {
            p_State->Settings.Display.Brightness = SETTINGS_DISPLAY_DEFAULT_BRIGHTNESS;
        }

        cJSON *timeout = cJSON_GetObjectItem(display, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.Display.Timeout = static_cast<uint16_t>(timeout->valueint);
        } else {
            p_State->Settings.Display.Timeout = SETTINGS_DISPLAY_DEFAULT_TIMEOUT;
        }
    } else {
        SettingsManager_InitDefaultDisplay(&p_State->Settings);
    }
}

/** @brief          Load the WiFi settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadWiFi(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *wifi = NULL;

    wifi = cJSON_GetObjectItem(p_JSON, "wifi");
    if (wifi != NULL) {
        cJSON *MaxRetries = cJSON_GetObjectItem(wifi, "MaxRetries");
        if (cJSON_IsNumber(MaxRetries)) {
            p_State->Settings.WiFi.MaxRetries = static_cast<uint8_t>(MaxRetries->valueint);
        } else {
            p_State->Settings.WiFi.MaxRetries = SETTINGS_WIFI_DEFAULT_MAX_RETRIES;
        }

        cJSON *RetryInterval = cJSON_GetObjectItem(wifi, "RetryInterval");
        if (cJSON_IsNumber(RetryInterval)) {
            p_State->Settings.WiFi.RetryInterval = static_cast<uint32_t>(RetryInterval->valueint);
        } else {
            p_State->Settings.WiFi.RetryInterval = SETTINGS_WIFI_DEFAULT_RETRY_INTERVAL;
        }

        cJSON *AutoConnect = cJSON_GetObjectItem(wifi, "AutoConnect");
        if (cJSON_IsBool(AutoConnect)) {
            p_State->Settings.WiFi.AutoConnect = cJSON_IsTrue(AutoConnect);
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

/** @brief          Load the Provisioning settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadProvisioning(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
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
            p_State->Settings.Provisioning.Timeout = static_cast<uint32_t>(timeout->valueint);
        } else {
            p_State->Settings.Provisioning.Timeout = SETTINGS_PROVISIONING_DEFAULT_TIMEOUT;
        }
    } else {
        SettingsManager_InitDefaultProvisioning(&p_State->Settings);
    }
}

/** @brief          Load the System settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadSystem(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
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

        cJSON *ImageFormat = cJSON_GetObjectItem(system, "ImageFormat");
        if (cJSON_IsString(ImageFormat)) {
            if (strcmp(ImageFormat->valuestring, "PNG") == 0) {
                p_State->Settings.System.ImageFormat = IMAGE_FORMAT_PNG;
            } else if (strcmp(ImageFormat->valuestring, "RAW") == 0) {
                p_State->Settings.System.ImageFormat = IMAGE_FORMAT_RAW;
            } else if (strcmp(ImageFormat->valuestring, "JPEG") == 0) {
                p_State->Settings.System.ImageFormat = IMAGE_FORMAT_JPEG;
            } else if (strcmp(ImageFormat->valuestring, "BITMAP") == 0 || strcmp(ImageFormat->valuestring, "BMP") == 0) {
                p_State->Settings.System.ImageFormat = IMAGE_FORMAT_BITMAP;
            } else {
                p_State->Settings.System.ImageFormat = IMAGE_FORMAT_JPEG;  /* Default to JPEG */
            }
        } else {
            p_State->Settings.System.ImageFormat = IMAGE_FORMAT_JPEG;  /* Default to JPEG */
        }

        cJSON *JpegQuality = cJSON_GetObjectItem(system, "JpegQuality");
        if (cJSON_IsNumber(JpegQuality)) {
            p_State->Settings.System.JpegQuality = static_cast<uint8_t>(JpegQuality->valueint);
            /* Clamp to valid range 1-100 */
            if (p_State->Settings.System.JpegQuality < 1) {
                p_State->Settings.System.JpegQuality = 1;
            } else if (p_State->Settings.System.JpegQuality > 100) {
                p_State->Settings.System.JpegQuality = 100;
            }
        } else {
            p_State->Settings.System.JpegQuality = 80;  /* Default to 80 */
        }
    } else {
        SettingsManager_InitDefaultSystem(&p_State->Settings);
    }
}

/** @brief          Load the HTTP server settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadHTTPServer(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *http_server = NULL;

    http_server = cJSON_GetObjectItem(p_JSON, "http-server");
    if (http_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(http_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.HTTPServer.Port = static_cast<uint16_t>(port->valueint);
        } else {
            p_State->Settings.HTTPServer.Port = SETTINGS_DEFAULT_HTTP_PORT;
        }

        cJSON *WsPingIntervalSec = cJSON_GetObjectItem(http_server, "WsPingIntervalSec");
        if (cJSON_IsNumber(WsPingIntervalSec)) {
            p_State->Settings.HTTPServer.WSPingIntervalSec = static_cast<uint16_t>(WsPingIntervalSec->valueint);
        } else {
            p_State->Settings.HTTPServer.WSPingIntervalSec = SETTINGS_DEFAULT_WS_PING_INTERVAL;
        }

        cJSON *MaxClients = cJSON_GetObjectItem(http_server, "MaxClients");
        if (cJSON_IsNumber(MaxClients)) {
            p_State->Settings.HTTPServer.MaxClients = static_cast<uint8_t>(MaxClients->valueint);
        } else {
            p_State->Settings.HTTPServer.MaxClients = SETTINGS_DEFAULT_HTTP_MAX_CLIENTS;
        }

        cJSON *UseCORS = cJSON_GetObjectItem(http_server, "enable-cors");
        if (cJSON_IsBool(UseCORS)) {
            p_State->Settings.HTTPServer.UseCORS = cJSON_IsTrue(UseCORS);
        } else {
            p_State->Settings.HTTPServer.UseCORS = SETTINGS_DEFAULT_HTTP_ENABLE_CORS;
        }

        cJSON *ApiKey = cJSON_GetObjectItem(http_server, "api-key");
        if (cJSON_IsString(ApiKey)) {
            strncpy(p_State->Settings.HTTPServer.APIKey, ApiKey->valuestring, sizeof(p_State->Settings.HTTPServer.APIKey));
        } else {
            strncpy(p_State->Settings.HTTPServer.APIKey, SETTINGS_DEFAULT_HTTP_API_KEY,
                    sizeof(p_State->Settings.HTTPServer.APIKey));
        }
    } else {
        SettingsManager_InitDefaultHTTPServer(&p_State->Settings);
    }
}

/** @brief          Load the VISA server settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadVISAServer(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *visa_server = NULL;

    visa_server = cJSON_GetObjectItem(p_JSON, "visa-server");
    if (visa_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(visa_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.VISAServer.Port = static_cast<uint16_t>(port->valueint);
        } else {
            p_State->Settings.VISAServer.Port = SETTINGS_DEFAULT_VISA_PORT;
        }

        cJSON *timeout = cJSON_GetObjectItem(visa_server, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.VISAServer.Timeout = static_cast<uint16_t>(timeout->valueint);
        } else {
            p_State->Settings.VISAServer.Timeout = SETTINGS_DEFAULT_VISA_TIMEOUT_MS;
        }
    } else {
        SettingsManager_InitDefaultVISAServer(&p_State->Settings);
    }
}

/** @brief          Load the LED flash settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadLEDFlash(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *led_flash = NULL;

    led_flash = cJSON_GetObjectItem(p_JSON, "led-flash");
    if (led_flash != NULL) {
        cJSON *enable = cJSON_GetObjectItem(led_flash, "enable");
        if (cJSON_IsBool(enable)) {
            p_State->Settings.LEDFlash.Enable = cJSON_IsTrue(enable);
        } else {
            p_State->Settings.LEDFlash.Enable = SETTINGS_DEFAULT_LED_FLASH_ENABLE;
        }

        cJSON *power = cJSON_GetObjectItem(led_flash, "power");
        if (cJSON_IsNumber(power)) {
            p_State->Settings.LEDFlash.Power = static_cast<uint8_t>(power->valueint);
        } else {
            p_State->Settings.LEDFlash.Power = SETTINGS_DEFAULT_LED_FLASH_POWER;
        }
    } else {
        SettingsManager_InitDefaultLEDFlash(&p_State->Settings);
    }
}

/** @brief          Load the USB settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadUSB(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *usb = NULL;

    usb = cJSON_GetObjectItem(p_JSON, "usb");
    if (usb != NULL) {
        cJSON *msc_enabled = cJSON_GetObjectItem(usb, "msc-enabled");
        if (cJSON_IsBool(msc_enabled)) {
            p_State->Settings.USB.MSC_Enabled = cJSON_IsTrue(msc_enabled);
        } else {
            p_State->Settings.USB.MSC_Enabled = SETTINGS_DEFAULT_USB_MSC_ENABLE;
        }

        cJSON *uvc_enabled = cJSON_GetObjectItem(usb, "uvc-enabled");
        if (cJSON_IsBool(uvc_enabled)) {
            p_State->Settings.USB.UVC_Enabled = cJSON_IsTrue(uvc_enabled);
        } else {
            p_State->Settings.USB.UVC_Enabled = SETTINGS_DEFAULT_USB_UVC_ENABLE;
        }

        cJSON *cdc_enabled = cJSON_GetObjectItem(usb, "cdc-enabled");
        if (cJSON_IsBool(cdc_enabled)) {
            p_State->Settings.USB.CDC_Enabled = cJSON_IsTrue(cdc_enabled);
        } else {
            p_State->Settings.USB.CDC_Enabled = SETTINGS_DEFAULT_USB_CDC_ENABLE;
        }
    } else {
        SettingsManager_InitDefaultUSB(&p_State->Settings);
    }
}

/** @brief          Load the calibration settings from the JSON object and apply them to the Settings Manager state. If a setting is missing or invalid, the default value is used.
 *  @param p_State  The Settings Manager state structure to update with the loaded settings
 *  @param p_JSON   The cJSON object representing the root of the settings JSON document
 */
static void SettingsManager_LoadCalibration(Settings_Manager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *calibration = NULL;

    calibration = cJSON_GetObjectItem(p_JSON, "calibration");
    if (calibration != NULL) {
        cJSON *room_temperature = cJSON_GetObjectItem(calibration, "room-temperature");
        if (cJSON_IsNumber(room_temperature)) {
            p_State->Settings.Calibration.RoomTemperature = static_cast<int16_t>(room_temperature->valuedouble);
        } else {
            p_State->Settings.Calibration.RoomTemperature = SETTINGS_DEFAULT_CALIBRATION_ROOM_TEMP;
        }

        cJSON *interval = cJSON_GetObjectItem(calibration, "interval");
        if (cJSON_IsNumber(interval)) {
            p_State->Settings.Calibration.Interval = static_cast<uint32_t>(interval->valuedouble);
        } else {
            p_State->Settings.Calibration.Interval = SETTINGS_DEFAULT_CALIBRATION_INTERVAL;
        }

        /* 0.0f is the "never calibrated" sentinel: the LeptonTask will apply zero offset
        * until the user explicitly performs a calibration (which stores the actual sensor
        * reading into SensorAtCalibration). */
        p_State->Settings.Calibration.SensorAtCalibration =
            0.0f;  /* The sensor reading at calibration time cannot be meaningfully set from the JSON, so we initialize it to the "never calibrated" sentinel value. */
    } else {
        SettingsManager_InitDefaultCalibration(&p_State->Settings);
    }
}

esp_err_t SettingsManager_LoadFromJSON(Settings_Manager_State_t *p_State, const char *p_FilePath)
{
    FILE *File = NULL;
    char *Buffer = NULL;
    long FileSize;
    size_t BytesRead;
    cJSON *JSON = NULL;
    esp_err_t Error;
    uint32_t Caps;

    ESP_LOGD(TAG, "Loading JSON settings from: %s", p_FilePath);

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

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    Caps = MALLOC_CAP_8BIT;
#endif

    /* Allocate buffer for file content */
    Buffer = static_cast<char *>(heap_caps_malloc(FileSize + 1, Caps));
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

    ESP_LOGI(TAG, "JSON parsed successfully from %s", p_FilePath);

    /* Check the version number of the JSON and the version from the firmware. Skip the loading if the version in the JSON is older or invalid. */
    cJSON *version = cJSON_GetObjectItem(JSON, "version");
    if (cJSON_IsNumber(version)) {
        p_State->Settings.Version = static_cast<uint32_t>(version->valueint);

        if (p_State->Settings.Version != SETTINGS_VERSION) {
            ESP_LOGW(TAG, "Settings version mismatch (expected %u, got %u), erasing and using defaults",
                     SETTINGS_VERSION, p_State->Settings.Version);

            Error = ESP_ERR_INVALID_VERSION;

            goto SettingsManager_Load_JSON_Exit;
        }
    } else {
        Error = ESP_ERR_INVALID_VERSION;

        goto SettingsManager_Load_JSON_Exit;
    }

    Error = ESP_OK;

    /* Load settings sections */
    SettingsManager_LoadDisplay(p_State, JSON);

    /* Extract Provisioning settings */
    SettingsManager_LoadProvisioning(p_State, JSON);

    /* Extract WiFi settings */
    SettingsManager_LoadWiFi(p_State, JSON);

    /* Extract System settings */
    SettingsManager_LoadSystem(p_State, JSON);

    /* Extract Lepton settings */
    SettingsManager_LoadLepton(p_State, JSON);

    /* Extract HTTP Server settings */
    SettingsManager_LoadHTTPServer(p_State, JSON);

    /* Extract VISA Server settings */
    SettingsManager_LoadVISAServer(p_State, JSON);

    /* Extract LED flash settings */
    SettingsManager_LoadLEDFlash(p_State, JSON);

    /* Extract USB settings */
    SettingsManager_LoadUSB(p_State, JSON);

    /* Extract Calibration settings */
    SettingsManager_LoadCalibration(p_State, JSON);

SettingsManager_Load_JSON_Exit:
    cJSON_Delete(JSON);

    return Error;
}
