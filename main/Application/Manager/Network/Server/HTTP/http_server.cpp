/*
 * http_server.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP server implementation.
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
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>

#include <sys/time.h>

#include <cJSON.h>
#include <cstring>
#include <strings.h>

#include "http_server.h"
#include "lepton_palette.h"
#include "../../../ImageEncoder/imageEncoder.h"
#include "../../Provisioning/provisionHandlers.h"
#include "../../SNTP/sntp.h"
#include "../../../Devices/devicesManager.h"
#include "../../../Network/networkManager.h"
#include "../../../Memory/memoryManager.h"

#define HTTP_SERVER_API_BASE_PATH           "/api/v1"
#define HTTP_SERVER_API_KEY_HEADER          "X-API-Key"

typedef struct {
    bool IsInitialized;
    bool IsRunning;
    httpd_handle_t Handle;
    Network_HTTP_Server_Config_t Config;
    ImageEncoder_Raw_t *RawFrame;
    uint32_t RequestCount;
    uint32_t StartTime;
    float LeptonFPA;
    float LeptonAUX;
    float DeviceTemperatureC;
} HTTP_Server_State_t;

static HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Server";

/** @brief              Check API key authentication.
 *  @param p_Request    HTTP request handle
 *  @return             true if authenticated
 */
static bool HTTP_Server_CheckAuth(httpd_req_t *p_Request)
{
    char ApiKey[64] = {};

    if (_HTTP_Server_State.Config.API_Key[0] == '\0') {
        return true;
    } else if (httpd_req_get_hdr_value_str(p_Request, HTTP_SERVER_API_KEY_HEADER, ApiKey, sizeof(ApiKey)) != ESP_OK) {
        return false;
    }

    return (strncmp(ApiKey, _HTTP_Server_State.Config.API_Key, sizeof(ApiKey)) == 0);
}

/** @brief              Send JSON response.
 *  @param p_Request    HTTP request handle
 *  @param p_JSON       JSON object to send
 *  @param StatusCode   HTTP status code
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Server_SendJSON(httpd_req_t *p_Request, cJSON *p_JSON, int StatusCode)
{
    esp_err_t Error;
    char *JSON;

    JSON = cJSON_PrintUnformatted(p_JSON);
    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encoding failed");

        return ESP_FAIL;
    }

    if (StatusCode != 200) {
        char StatusStr[16];
        snprintf(StatusStr, sizeof(StatusStr), "%d", StatusCode);
        httpd_resp_set_status(p_Request, StatusStr);
    }

    httpd_resp_set_type(p_Request, "application/json");

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_sendstr(p_Request, JSON);
    cJSON_free(JSON);

    return Error;
}

/** @brief              Send error response.
 *  @param p_Request    HTTP request handle
 *  @param StatusCode   HTTP status code
 *  @param p_Message    Error message
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Server_SendError(httpd_req_t *p_Request, int StatusCode, const char *p_Message)
{
    esp_err_t Error;
    cJSON *JSON;

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");

        return ESP_FAIL;
    }

    cJSON_AddStringToObject(JSON, "error", p_Message);
    cJSON_AddNumberToObject(JSON, "code", StatusCode);

    Error = HTTP_Server_SendJSON(p_Request, JSON, StatusCode);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Parse JSON from request body.
 *  @param p_Request    HTTP request handle
 *  @return             cJSON object or NULL on error
 */
static cJSON *HTTP_Server_ParseJSON(httpd_req_t *p_Request)
{
    char *Buffer;
    cJSON *JSON;

    if ((p_Request->content_len <= 0) || (p_Request->content_len > 4096)) {
        return NULL;
    }

    Buffer = static_cast<char *>(heap_caps_malloc(p_Request->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (Buffer == NULL) {
        return NULL;
    }

    if (httpd_req_recv(p_Request, Buffer, p_Request->content_len) != p_Request->content_len) {
        heap_caps_free(Buffer);

        return NULL;
    }

    Buffer[p_Request->content_len] = '\0';
    JSON = cJSON_Parse(Buffer);
    heap_caps_free(Buffer);

    return JSON;
}

/** @brief              Handler for POST /api/v1/time.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Time(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Epoch;
    cJSON *Timezone;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = HTTP_Server_ParseJSON(p_Request);
    if (JSON == NULL) {
        return HTTP_Server_SendError(p_Request, 400, "Invalid JSON");
    }

    Epoch = cJSON_GetObjectItem(JSON, "epoch");
    Timezone = cJSON_GetObjectItem(JSON, "timezone");

    if (cJSON_IsNumber(Epoch) == false) {
        cJSON_Delete(JSON);

        return HTTP_Server_SendError(p_Request, 400, "Missing epoch field");
    }

    /* Set system time */
    struct timeval tv = {
        .tv_sec = static_cast<time_t>(Epoch->valuedouble),
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);

    /* Set timezone if provided */
    if (cJSON_IsString(Timezone) && (Timezone->valuestring != NULL)) {
        SNTP_SetTimezone(Timezone->valuestring);
    }

    ESP_LOGD(TAG, "Time set to epoch: %f", Epoch->valuedouble);

    cJSON_Delete(JSON);

    /* Send response */
    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for GET /api/v1/image.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Image(httpd_req_t *p_Request)
{
    esp_err_t Error;
    ImageEncoder_EncodedImage_t Encoded;
    ImageEncoder_Format_t Format = IMAGE_FORMAT_JPEG;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    } else if (_HTTP_Server_State.RawFrame == NULL) {
        return HTTP_Server_SendError(p_Request, 503, "No frame data available");
    }

    if (xSemaphoreTake(_HTTP_Server_State.RawFrame->Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return HTTP_Server_SendError(p_Request, 503, "Frame busy");
    }

    if ((_HTTP_Server_State.RawFrame->Width == 0) || (_HTTP_Server_State.RawFrame->Height == 0)) {
        xSemaphoreGive(_HTTP_Server_State.RawFrame->Mutex);
        return HTTP_Server_SendError(p_Request, 503, "No valid frame available");
    }

    Error = ImageEncoder_Encode(_HTTP_Server_State.RawFrame, Format, &Encoded);

    xSemaphoreGive(_HTTP_Server_State.RawFrame->Mutex);

    if (Error != ESP_OK) {
        return HTTP_Server_SendError(p_Request, 500, "Image encoding failed");
    }

    switch (Format) {
        case IMAGE_FORMAT_JPEG: {
            httpd_resp_set_type(p_Request, "image/jpeg");

            break;
        }
        case IMAGE_FORMAT_PNG: {
            httpd_resp_set_type(p_Request, "image/png");

            break;
        }
        case IMAGE_FORMAT_RAW: {
            httpd_resp_set_type(p_Request, "application/octet-stream");

            break;
        }
        case IMAGE_FORMAT_BITMAP: {
            httpd_resp_set_type(p_Request, "image/bmp");

            break;
        }
        default: {
            ImageEncoder_Free(&Encoded);

            return HTTP_Server_SendError(p_Request, 500, "Unknown image format");
        }
    }

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_send(p_Request, reinterpret_cast<const char *>(Encoded.Data), Encoded.Size);

    ImageEncoder_Free(&Encoded);

    return Error;
}

/** @brief              Handler for GET /api/v1/settings — returns current settings as JSON.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Settings_GET(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Display;
    cJSON *System;
    cJSON *Calibration;
    cJSON *LED;
    Settings_Display_t DispSettings;
    Settings_System_t SysSettings;
    Settings_Calibration_t CalSettings;
    Settings_LED_Flash_t LEDSettings;
    Settings_Lepton_t *p_LeptonSettings;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    /* Settings_Lepton_t contains 128 emissivity presets (~4.6 KB) — allocate on heap */
    p_LeptonSettings = static_cast<Settings_Lepton_t *>(
        heap_caps_malloc(sizeof(Settings_Lepton_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (p_LeptonSettings == NULL) {
        return HTTP_Server_SendError(p_Request, 500, "Out of memory");
    }

    SettingsManager_GetLepton(p_LeptonSettings);
    SettingsManager_GetDisplay(&DispSettings);
    SettingsManager_GetSystem(&SysSettings);
    SettingsManager_GetCalibration(&CalSettings);
    SettingsManager_GetLEDFlash(&LEDSettings);

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        heap_caps_free(p_LeptonSettings);

        return HTTP_Server_SendError(p_Request, 500, "JSON allocation failed");
    }

    cJSON_AddNumberToObject(JSON, "palette", p_LeptonSettings->Palette);

    Display = cJSON_AddObjectToObject(JSON, "display");
    cJSON_AddNumberToObject(Display, "brightness", DispSettings.Brightness);
    cJSON_AddNumberToObject(Display, "timeout", DispSettings.Timeout);

    System = cJSON_AddObjectToObject(JSON, "system");
    cJSON_AddNumberToObject(System, "image_format", static_cast<int>(SysSettings.ImageFormat));
    cJSON_AddNumberToObject(System, "jpeg_quality", SysSettings.JpegQuality);
    cJSON_AddStringToObject(System, "device_name", SysSettings.DeviceName);
    cJSON_AddStringToObject(System, "timezone", SysSettings.Timezone);

    Calibration = cJSON_AddObjectToObject(JSON, "calibration");
    cJSON_AddNumberToObject(Calibration, "room_temperature", CalSettings.RoomTemperature);
    cJSON_AddNumberToObject(Calibration, "interval", static_cast<double>(CalSettings.Interval));
    cJSON_AddNumberToObject(Calibration, "sensor_at_calibration", static_cast<double>(CalSettings.SensorAtCalibration));

    LED = cJSON_AddObjectToObject(JSON, "led");
    cJSON_AddBoolToObject(LED, "enable", LEDSettings.Enable);
    cJSON_AddNumberToObject(LED, "power", LEDSettings.Power);

    heap_caps_free(p_LeptonSettings);

    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for POST /api/v1/settings — updates one or more settings.
 *                      All fields are optional; only keys present in the JSON body are applied.
 *                      Supported keys:
 *                        "palette"                       (number) — active color palette index
 *                        "display.brightness"            (number) — display brightness 0-100
 *                        "display.timeout"               (number) — screen timeout in seconds
 *                        "system.image_format"           (number) — ImageEncoder_Format_t value
 *                        "system.jpeg_quality"           (number) — JPEG quality 1-100
 *                        "system.device_name"            (string) — device name
 *                        "system.timezone"               (string) — POSIX timezone string
 *                        "calibration.room_temperature"  (number) — ambient temperature in °C
 *                        "calibration.interval"          (number) — auto-calibration interval in seconds
 *                        "led.enable"                    (bool)   — LED flash enable
 *                        "led.power"                     (number) — LED flash power 0-100
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Settings_POST(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Item;
    cJSON *Group;
    SettingsManager_ChangeNotification_t Changed;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = HTTP_Server_ParseJSON(p_Request);
    if (JSON == NULL) {
        return HTTP_Server_SendError(p_Request, 400, "Invalid JSON");
    }

    /* --- palette --- */
    Item = cJSON_GetObjectItem(JSON, "palette");
    if (Item != NULL) {
        if (cJSON_IsNumber(Item) == false) {
            cJSON_Delete(JSON);

            return HTTP_Server_SendError(p_Request, 400, "Invalid 'palette' field");
        }

        int PaletteIdx = Item->valueint;

        if ((PaletteIdx < 0) || (PaletteIdx >= static_cast<int>(LEPTON_PALETTE_COUNT))) {
            cJSON_Delete(JSON);

            return HTTP_Server_SendError(p_Request, 400, "Palette index out of range");
        }

        /* Settings_Lepton_t contains 128 emissivity presets (~4.6 KB) — allocate on heap */
        Settings_Lepton_t *p_LeptonSettings = static_cast<Settings_Lepton_t *>(
            heap_caps_malloc(sizeof(Settings_Lepton_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (p_LeptonSettings == NULL) {
            cJSON_Delete(JSON);

            return HTTP_Server_SendError(p_Request, 500, "Out of memory");
        }

        SettingsManager_GetLepton(p_LeptonSettings);
        p_LeptonSettings->Palette = static_cast<uint8_t>(PaletteIdx);
        Changed.ID = SETTINGS_ID_LEPTON_PALETTE;
        Changed.Value = static_cast<uint32_t>(PaletteIdx);
        SettingsManager_UpdateLepton(p_LeptonSettings, &Changed);
        heap_caps_free(p_LeptonSettings);

        ESP_LOGD(TAG, "Settings: palette -> %d", PaletteIdx);
    }

    /* --- display --- */
    Group = cJSON_GetObjectItem(JSON, "display");
    if (Group != NULL) {
        Settings_Display_t DispSettings;

        SettingsManager_GetDisplay(&DispSettings);

        Item = cJSON_GetObjectItem(Group, "brightness");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            DispSettings.Brightness = static_cast<uint8_t>(Item->valueint);
            Changed.ID = SETTINGS_ID_DISPLAY_BRIGHTNESS;
            Changed.Value = static_cast<uint32_t>(DispSettings.Brightness);
            SettingsManager_UpdateDisplay(&DispSettings, &Changed);

            ESP_LOGD(TAG, "Settings: display.brightness -> %u", DispSettings.Brightness);
        }

        Item = cJSON_GetObjectItem(Group, "timeout");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            DispSettings.Timeout = static_cast<uint16_t>(Item->valueint);
            SettingsManager_GetDisplay(&DispSettings);
            DispSettings.Timeout = static_cast<uint16_t>(Item->valueint);
            SettingsManager_UpdateDisplay(&DispSettings, NULL);

            ESP_LOGD(TAG, "Settings: display.timeout -> %u", DispSettings.Timeout);
        }
    }

    /* --- system --- */
    Group = cJSON_GetObjectItem(JSON, "system");
    if (Group != NULL) {
        Settings_System_t SysSettings;

        SettingsManager_GetSystem(&SysSettings);

        Item = cJSON_GetObjectItem(Group, "image_format");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            SysSettings.ImageFormat = static_cast<ImageEncoder_Format_t>(Item->valueint);
            Changed.ID = SETTINGS_ID_IMAGE_FORMAT;
            Changed.Value = static_cast<uint32_t>(SysSettings.ImageFormat);
            SettingsManager_UpdateSystem(&SysSettings, &Changed);

            ESP_LOGD(TAG, "Settings: system.image_format -> %d", static_cast<int>(SysSettings.ImageFormat));
        }

        Item = cJSON_GetObjectItem(Group, "jpeg_quality");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            SettingsManager_GetSystem(&SysSettings);
            SysSettings.JpegQuality = static_cast<uint8_t>(Item->valueint);
            SettingsManager_UpdateSystem(&SysSettings, NULL);

            ESP_LOGD(TAG, "Settings: system.jpeg_quality -> %u", SysSettings.JpegQuality);
        }

        Item = cJSON_GetObjectItem(Group, "device_name");
        if ((Item != NULL) && cJSON_IsString(Item) && (Item->valuestring != NULL)) {
            SettingsManager_GetSystem(&SysSettings);
            strncpy(SysSettings.DeviceName, Item->valuestring, sizeof(SysSettings.DeviceName) - 1);
            SysSettings.DeviceName[sizeof(SysSettings.DeviceName) - 1] = '\0';
            SettingsManager_UpdateSystem(&SysSettings, NULL);

            ESP_LOGD(TAG, "Settings: system.device_name -> %s", SysSettings.DeviceName);
        }

        Item = cJSON_GetObjectItem(Group, "timezone");
        if ((Item != NULL) && cJSON_IsString(Item) && (Item->valuestring != NULL)) {
            SettingsManager_GetSystem(&SysSettings);
            strncpy(SysSettings.Timezone, Item->valuestring, sizeof(SysSettings.Timezone) - 1);
            SysSettings.Timezone[sizeof(SysSettings.Timezone) - 1] = '\0';
            Changed.ID = SETTINGS_ID_SNTP_TIMEZONE;
            Changed.Value = 0;
            SettingsManager_UpdateSystem(&SysSettings, &Changed);

            ESP_LOGD(TAG, "Settings: system.timezone -> %s", SysSettings.Timezone);
        }
    }

    /* --- calibration --- */
    Group = cJSON_GetObjectItem(JSON, "calibration");
    if (Group != NULL) {
        Settings_Calibration_t CalSettings;

        SettingsManager_GetCalibration(&CalSettings);

        Item = cJSON_GetObjectItem(Group, "room_temperature");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            CalSettings.RoomTemperature = static_cast<int16_t>(Item->valueint);
            Changed.ID = SETTINGS_ID_CALIBRATION_ROOM_TEMP;
            Changed.Value = static_cast<uint32_t>(CalSettings.RoomTemperature);
            SettingsManager_UpdateCalibration(&CalSettings, &Changed);

            ESP_LOGD(TAG, "Settings: calibration.room_temperature -> %d", CalSettings.RoomTemperature);
        }

        Item = cJSON_GetObjectItem(Group, "interval");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            SettingsManager_GetCalibration(&CalSettings);
            CalSettings.Interval = static_cast<uint32_t>(Item->valuedouble);
            Changed.ID = SETTINGS_ID_CALIBRATION_INTERVAL;
            Changed.Value = CalSettings.Interval;
            SettingsManager_UpdateCalibration(&CalSettings, &Changed);

            ESP_LOGD(TAG, "Settings: calibration.interval -> %u", CalSettings.Interval);
        }
    }

    /* --- led --- */
    Group = cJSON_GetObjectItem(JSON, "led");
    if (Group != NULL) {
        Settings_LED_Flash_t LEDSettings;

        SettingsManager_GetLEDFlash(&LEDSettings);

        Item = cJSON_GetObjectItem(Group, "enable");
        if ((Item != NULL) && cJSON_IsBool(Item)) {
            LEDSettings.Enable = (cJSON_IsTrue(Item) == 1);
            Changed.ID = SETTINGS_ID_LED_FLASH_ENABLE;
            Changed.Value = static_cast<uint32_t>(LEDSettings.Enable);
            SettingsManager_UpdateLEDFlash(&LEDSettings, &Changed);

            ESP_LOGD(TAG, "Settings: led.enable -> %d", static_cast<int>(LEDSettings.Enable));
        }

        Item = cJSON_GetObjectItem(Group, "power");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            SettingsManager_GetLEDFlash(&LEDSettings);
            LEDSettings.Power = static_cast<uint8_t>(Item->valueint);
            Changed.ID = SETTINGS_ID_LED_FLASH_POWER;
            Changed.Value = static_cast<uint32_t>(LEDSettings.Power);
            SettingsManager_UpdateLEDFlash(&LEDSettings, &Changed);

            ESP_LOGD(TAG, "Settings: led.power -> %u", LEDSettings.Power);
        }
    }

    cJSON_Delete(JSON);
    SettingsManager_Save();

    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "ok");
    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for GET /api/v1/telemetry.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Telemetry(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    DevicesManager_Battery_Status_t BatteryStatus;
    float Temperature;
    int8_t RSSI;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    RSSI = NetworkManager_GetRSSI();
    DevicesManager_GetBatteryStatus(&BatteryStatus);
    DevicesManager_GetTemperature(&Temperature);

    JSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(JSON, "device_uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(JSON, "battery_voltage_mv", BatteryStatus.Voltage);
    cJSON_AddNumberToObject(JSON, "battery_percentage", BatteryStatus.Percentage);
    cJSON_AddBoolToObject(JSON, "battery_charging", BatteryStatus.IsCharging);
    cJSON_AddNumberToObject(JSON, "wifi_rssi_dbm", RSSI);
    cJSON_AddNumberToObject(JSON, "temperature_c", Temperature);
    cJSON_AddNumberToObject(JSON, "lepton_fpa_c", _HTTP_Server_State.LeptonFPA);
    cJSON_AddNumberToObject(JSON, "lepton_aux_c", _HTTP_Server_State.LeptonAUX);
    cJSON_AddNumberToObject(JSON, "device_temp_c", _HTTP_Server_State.DeviceTemperatureC);

    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

static esp_err_t HTTP_Handler_Info(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Palettes;
    cJSON *ImageFormats;
    char Buffer[32];

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = cJSON_CreateObject();
    snprintf(Buffer, sizeof(Buffer), "Firmware %u.%u.%u", PYROVISION_VERSION_MAJOR, PYROVISION_VERSION_MINOR, PYROVISION_VERSION_BUILD);
    cJSON_AddStringToObject(JSON, "firmware_version", Buffer);
    cJSON_AddStringToObject(JSON, "build_date", __DATE__ " " __TIME__);

    Palettes = cJSON_AddArrayToObject(JSON, "palettes");
    for (size_t i = 0; i < LEPTON_PALETTE_COUNT; i++) {
        if (Lepton_Palette_Names[i] == NULL) {
            continue;
        }

        cJSON *Entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(Entry, "index", static_cast<double>(i));
        cJSON_AddStringToObject(Entry, "name", Lepton_Palette_Names[i]);
        cJSON_AddItemToArray(Palettes, Entry);
    }

    ImageFormats = cJSON_AddArrayToObject(JSON, "image_formats");
    for (size_t i = 0; i < IMAGE_FORMAT_COUNT; i++) {
        if (ImageEncoder_Format_Names[i] == NULL) {
            continue;
        }

        cJSON *Entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(Entry, "index", static_cast<double>(i));
        cJSON_AddStringToObject(Entry, "name", ImageEncoder_Format_Names[i]);
        cJSON_AddItemToArray(ImageFormats, Entry);
    }

    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for GET /api/v1/memory.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Memory(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Memory;
    cJSON *CoreDump;
    MemoryManager_Usage_t MemoryUsage;
    MemoryManager_Usage_t CoreDumpUsage;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    MemoryManager_GetStorageUsage(&MemoryUsage);
    MemoryManager_GetCoredumpUsage(&CoreDumpUsage);

    JSON = cJSON_CreateObject();

    cJSON_AddBoolToObject(JSON, "present", MemoryManager_HasSDCard());

    Memory = cJSON_CreateObject();
    cJSON_AddNumberToObject(Memory, "free_mb", MemoryUsage.FreeBytes / 1024 / 1024);
    cJSON_AddNumberToObject(Memory, "total_mb", MemoryUsage.TotalBytes / 1024 / 1024);
    cJSON_AddNumberToObject(Memory, "used_mb", MemoryUsage.UsedBytes / 1024 / 1024);
    cJSON_AddItemToObject(JSON, "sdcard", Memory);

    CoreDump = cJSON_CreateObject();
    cJSON_AddNumberToObject(CoreDump, "free_mb", CoreDumpUsage.FreeBytes / 1024 / 1024);
    cJSON_AddNumberToObject(CoreDump, "total_mb", CoreDumpUsage.TotalBytes / 1024 / 1024);
    cJSON_AddNumberToObject(CoreDump, "used_mb", CoreDumpUsage.UsedBytes / 1024 / 1024);
    cJSON_AddItemToObject(JSON, "coredump", CoreDump);

    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    return Error;
}

/** @brief              Handler for POST /api/v1/update (OTA).
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Update(httpd_req_t *p_Request)
{
    int Received;
    int Total_Received;
    esp_err_t Error;
    esp_ota_handle_t ota_handle;
    char *Buffer;
    cJSON *JSON;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL) {
        return HTTP_Server_SendError(p_Request, 500, "No OTA partition found");
    }

    ESP_LOGI(TAG, "OTA update starting, partition: %s", update_partition->label);

    Error = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: 0x%X!", Error);

        return HTTP_Server_SendError(p_Request, 500, "OTA begin failed");
    }

    /* Receive firmware data */
    Buffer = static_cast<char *>(heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (Buffer == NULL) {
        esp_ota_abort(ota_handle);

        return HTTP_Server_SendError(p_Request, 500, "Memory allocation failed");
    }

    Received = 0;
    Total_Received = 0;
    while (Total_Received < p_Request->content_len) {
        Received = httpd_req_recv(p_Request, Buffer, 1024);
        if (Received <= 0) {
            if (Received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }

            heap_caps_free(Buffer);
            esp_ota_abort(ota_handle);

            return HTTP_Server_SendError(p_Request, 500, "Receive failed");
        }

        Error = esp_ota_write(ota_handle, Buffer, Received);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: 0x%X!", Error);

            heap_caps_free(Buffer);
            esp_ota_abort(ota_handle);

            return HTTP_Server_SendError(p_Request, 500, "OTA write failed");
        }

        Total_Received += Received;
        ESP_LOGD(TAG, "OTA progress: %d/%d bytes", Total_Received, p_Request->content_len);
    }

    heap_caps_free(Buffer);

    Error = esp_ota_end(ota_handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: 0x%X", Error);

        return HTTP_Server_SendError(p_Request, 500, "OTA end failed");
    }

    Error = esp_ota_set_boot_partition(update_partition);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: 0x%X!", Error);

        return HTTP_Server_SendError(p_Request, 500, "Set boot partition failed");
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting...");

    /* Send response */
    JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(JSON, "status", "updating");
    cJSON_AddStringToObject(JSON, "message", "Firmware upload successful. Device will reboot after update.");
    Error = HTTP_Server_SendJSON(p_Request, JSON, 200);
    cJSON_Delete(JSON);

    /* Reboot after response is sent */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return Error;
}

/** @brief              Handler for CORS preflight requests.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
static esp_err_t HTTP_Handler_Options(httpd_req_t *p_Request)
{
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    httpd_resp_set_hdr(p_Request, "Access-Control-Max-Age", "86400");
    httpd_resp_send(p_Request, NULL, 0);

    return ESP_OK;
}

static const httpd_uri_t _URI_Time = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/time",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Time,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Image = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/image",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Image,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Settings_GET = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/settings",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Settings_GET,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Settings_POST = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/settings",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Settings_POST,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Telemetry = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/telemetry",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Telemetry,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Info = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/info",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Info,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Memory = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/memory",
    .method    = HTTP_GET,
    .handler   = HTTP_Handler_Memory,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Update = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/update",
    .method    = HTTP_POST,
    .handler   = HTTP_Handler_Update,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Options = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/*",
    .method    = HTTP_OPTIONS,
    .handler   = HTTP_Handler_Options,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Root,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Logo = {
    .uri       = "/logo.png",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Logo,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Scan = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/provision/scan",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Scan,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Connect = {
    .uri       = HTTP_SERVER_API_BASE_PATH "/provision",
    .method    = HTTP_POST,
    .handler   = Provision_Handler_Connect,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_Provision_Favicon = {
    .uri       = "/favicon.ico",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_Favicon,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_CaptivePortal_Generate204 = {
    .uri       = "/generate_204",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_CaptivePortal,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

static const httpd_uri_t _URI_CaptivePortal_Generate204_NoUnderscore = {
    .uri       = "/generate204",
    .method    = HTTP_GET,
    .handler   = Provision_Handler_CaptivePortal,
    .user_ctx  = NULL,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL,
};

esp_err_t HTTP_Server_Init(const Network_HTTP_Server_Config_t *p_Config)
{
    if (p_Config == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_HTTP_Server_State.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized, updating config");

        memcpy(&_HTTP_Server_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing HTTP server");

    memcpy(&_HTTP_Server_State.Config, p_Config, sizeof(Network_HTTP_Server_Config_t));
    _HTTP_Server_State.Handle = NULL;
    _HTTP_Server_State.RawFrame = NULL;
    _HTTP_Server_State.RequestCount = 0;
    _HTTP_Server_State.IsInitialized = true;

    return ESP_OK;
}

void HTTP_Server_Deinit(void)
{
    if (_HTTP_Server_State.IsInitialized == false) {
        return;
    }

    HTTP_Server_Stop();
    _HTTP_Server_State.IsInitialized = false;

    ESP_LOGD(TAG, "HTTP server deinitialized");
}

esp_err_t HTTP_Server_Start(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_HTTP_Server_State.IsRunning) {
        ESP_LOGW(TAG, "Server already running");

        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _HTTP_Server_State.Config.Port;
    config.max_uri_handlers = 18;

    /* Use only 2 sockets for provisioning */
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    /* Very short timeouts to allow quick shutdown */
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.keep_alive_enable = false;  /* Disable keep-alive to reduce memory */

    /* Increased stack and header size for Android captive portal requests */
    config.stack_size = 6144;
    config.max_resp_headers = 8;
    config.max_req_hdr_len = 1024;  /* Android sends large headers */

    /* Core affinity and priority - run on core 1 to avoid blocking IDLE0 */
    config.ctrl_port = 0;
    config.core_id = 1;
    config.task_priority = 3;

    ESP_LOGD(TAG, "Starting HTTP server on port %d", config.server_port);

    Error = httpd_start(&_HTTP_Server_State.Handle, &config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: 0x%X!", Error);

        return Error;
    }

    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Root);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Logo);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Scan);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Connect);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Provision_Favicon);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_CaptivePortal_Generate204_NoUnderscore);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Time);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Image);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Settings_GET);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Settings_POST);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Telemetry);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Info);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Memory);
    httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Update);

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_register_uri_handler(_HTTP_Server_State.Handle, &_URI_Options);
    }

    _HTTP_Server_State.IsRunning = true;
    _HTTP_Server_State.StartTime = esp_timer_get_time() / 1000000;

    ESP_LOGD(TAG, "HTTP server started");

    return ESP_OK;
}

esp_err_t HTTP_Server_Stop(void)
{
    esp_err_t Error;

    if (_HTTP_Server_State.IsRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping HTTP server");

    _HTTP_Server_State.IsRunning = false;

    if (_HTTP_Server_State.Handle != NULL) {
        Error = httpd_stop(_HTTP_Server_State.Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop HTTP server: 0x%X", Error);
        }
        _HTTP_Server_State.Handle = NULL;
    }

    return ESP_OK;
}

bool HTTP_Server_IsRunning(void)
{
    return _HTTP_Server_State.IsRunning;
}

void HTTP_Server_SetRawFrame(ImageEncoder_Raw_t *p_Frame)
{
    _HTTP_Server_State.RawFrame = p_Frame;
}

httpd_handle_t HTTP_Server_GetHandle(void)
{
    return _HTTP_Server_State.Handle;
}

void HTTP_Server_SetLeptonTemperatures(float FPA, float Aux)
{
    _HTTP_Server_State.LeptonFPA = FPA;
    _HTTP_Server_State.LeptonAUX = Aux;
}

void HTTP_Server_SetDeviceTemperature(float Temperature)
{
    _HTTP_Server_State.DeviceTemperatureC = Temperature;
}