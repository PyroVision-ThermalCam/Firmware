/*
 * remoteControl.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common remote control interface implementation.
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

#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "remoteControl.h"
#include "../../../Time/timeManager.h"
#include "../../../Devices/devicesManager.h"
#include "../../../Settings/settingsManager.h"
#include "../../../Memory/memoryManager.h"
#include "../../../../Tasks/tasks.h"

static const char *TAG = "RemoteControl";

typedef struct {
    App_Lepton_ROI_Result_t Spotmeter;
    SemaphoreHandle_t Mutex;
    bool isValid;
} RemoteControl_State_t;

static RemoteControl_State_t _RemoteControl_State;

esp_err_t RemoteControl_GetTemperature(float *p_Temperature)
{
    if (p_Temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get temperature from TMP117 via DevicesManager */
    *p_Temperature = 25.0f;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_GetTime(char *p_TimeStr, size_t MaxLen)
{
    time_t Now;
    struct tm TimeInfo;

    if (p_TimeStr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time(&Now);
    localtime_r(&Now, &TimeInfo);

    /* ISO 8601 format: YYYY-MM-DDTHH:MM:SS */
    strftime(p_TimeStr, MaxLen, "%Y-%m-%dT%H:%M:%S", &TimeInfo);

    return ESP_OK;
}

esp_err_t RemoteControl_SetTime(const char *p_TimeStr)
{
    struct tm TimeInfo;
    time_t NewTime;

    if (p_TimeStr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS */
    if (strptime(p_TimeStr, "%Y-%m-%dT%H:%M:%S", &TimeInfo) == NULL) {
        ESP_LOGE(TAG, "Invalid time format. Expected: YYYY-MM-DDTHH:MM:SS");
        return ESP_ERR_INVALID_ARG;
    }

    NewTime = mktime(&TimeInfo);
    if (NewTime == -1) {
        ESP_LOGE(TAG, "Failed to convert time!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Set system time */
    struct timeval tv = {
        .tv_sec = NewTime,
        .tv_usec = 0,
    };

    settimeofday(&tv, NULL);

    ESP_LOGD(TAG, "System time set to: %s", p_TimeStr);

    return ESP_OK;
}

esp_err_t RemoteControl_GetBatteryVoltage(float *p_Voltage)
{
    if (p_Voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get battery voltage from ADC via DevicesManager */
    *p_Voltage = 3.7f;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_GetStateOfCharge(uint8_t *p_SOC)
{
    if (p_SOC == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Calculate SOC from battery voltage */
    *p_SOC = 75;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_GetOV5640Image(uint8_t **pp_Buffer, size_t *p_Size, Remote_Image_Format_t Format)
{
    if ((pp_Buffer == NULL) || (p_Size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get image from OV5640 camera */
    ESP_LOGW(TAG, "OV5640 image capture not implemented");

    return ESP_ERR_NOT_FOUND;
}

esp_err_t RemoteControl_GetLeptonImage(uint8_t **pp_Buffer, size_t *p_Size, Remote_Image_Format_t Format)
{
    if ((pp_Buffer == NULL) || (p_Size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get image from Lepton camera via LeptonTask */
    ESP_LOGW(TAG, "Lepton image capture not implemented");

    return ESP_ERR_NOT_FOUND;
}

esp_err_t RemoteControl_GetLeptonEmissivity(uint8_t *p_Emissivity)
{
    esp_err_t Error;
    App_Settings_Lepton_t Lepton;

    if (p_Emissivity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = SettingsManager_GetLepton(&Lepton);
    if(Error != ESP_OK) {
        return Error;
    }

    *p_Emissivity = Lepton.CurrentEmissivity;

    return ESP_OK;
}

esp_err_t RemoteControl_SetLeptonEmissivity(uint8_t Emissivity)
{
    esp_err_t Error;
    App_Settings_Lepton_t Lepton;
    SettingsManager_Setting_t Changed;

    if (Emissivity > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = SettingsManager_GetLepton(&Lepton);
    if(Error != ESP_OK) {
        return Error;
    }

    Lepton.CurrentEmissivity = Emissivity;
    Changed.ID = SETTINGS_ID_LEPTON_EMISSIVITY;
    Changed.Value = Emissivity;

    ESP_LOGD(TAG, "Set Lepton emissivity to: %u%%", Emissivity);

    return SettingsManager_UpdateLepton(&Lepton, &Changed);
}

esp_err_t RemoteControl_GetLeptonSceneStats(cJSON *p_JSON)
{
    if (p_JSON == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get scene statistics from Lepton camera */
    cJSON_AddNumberToObject(p_JSON, "min_temp", 15.5);
    cJSON_AddNumberToObject(p_JSON, "max_temp", 35.2);
    cJSON_AddNumberToObject(p_JSON, "avg_temp", 22.8);

    return ESP_OK;
}

esp_err_t RemoteControl_GetLeptonROI(cJSON *p_JSON)
{
    if (p_JSON == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get ROI from Lepton camera */
    cJSON_AddNumberToObject(p_JSON, "x", 40);
    cJSON_AddNumberToObject(p_JSON, "y", 30);
    cJSON_AddNumberToObject(p_JSON, "width", 80);
    cJSON_AddNumberToObject(p_JSON, "height", 60);

    return ESP_OK;
}

esp_err_t RemoteControl_SetLeptonROI(const cJSON *p_JSON)
{
    if (p_JSON == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *x = cJSON_GetObjectItem(p_JSON, "x");
    cJSON *y = cJSON_GetObjectItem(p_JSON, "y");
    cJSON *width = cJSON_GetObjectItem(p_JSON, "width");
    cJSON *height = cJSON_GetObjectItem(p_JSON, "height");

    if ((cJSON_IsNumber(x) == false) || (cJSON_IsNumber(y) == false) ||
        (cJSON_IsNumber(width) == false) || (cJSON_IsNumber(height) == false)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Set ROI in Lepton camera */
    ESP_LOGI(TAG, "Set Lepton ROI: x=%d, y=%d, w=%d, h=%d",
             x->valueint, y->valueint, width->valueint, height->valueint);

    return ESP_OK;
}

esp_err_t RemoteControl_UpdateSpotmeter(float Min, float Max, float Mean)
{
    /* Initialize mutex on first call */
    if (_RemoteControl_State.Mutex == NULL) {
        _RemoteControl_State.Mutex = xSemaphoreCreateMutex();
        if (_RemoteControl_State.Mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex for spotmeter data");
            return ESP_ERR_NO_MEM;
        }
    }

    /* Thread-safe update of spotmeter data */
    if (xSemaphoreTake(_RemoteControl_State.Mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        _RemoteControl_State.Spotmeter.Min = Min;
        _RemoteControl_State.Spotmeter.Max = Max;
        _RemoteControl_State.Spotmeter.Mean = Mean;
        _RemoteControl_State.isValid = true;
        xSemaphoreGive(_RemoteControl_State.Mutex);

        ESP_LOGD(TAG, "Spotmeter data updated: Min=%.2f°C, Max=%.2f°C, Avg=%.2f°C",
                 _RemoteControl_State.Spotmeter.Min, _RemoteControl_State.Spotmeter.Max, _RemoteControl_State.Spotmeter.Mean);

        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex for spotmeter update!");

        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t RemoteControl_GetLeptonSpotmeter(cJSON *p_JSON)
{
    if (p_JSON == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if data is available */
    if (_RemoteControl_State.isValid == false) {
        ESP_LOGW(TAG, "No spotmeter data available yet");
        return ESP_ERR_NOT_FOUND;
    }

    /* Thread-safe read of spotmeter data */
    if (xSemaphoreTake(_RemoteControl_State.Mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        cJSON_AddNumberToObject(p_JSON, "min", _RemoteControl_State.Spotmeter.Min);
        cJSON_AddNumberToObject(p_JSON, "max", _RemoteControl_State.Spotmeter.Max);
        cJSON_AddNumberToObject(p_JSON, "average", _RemoteControl_State.Spotmeter.Average);
        cJSON_AddNumberToObject(p_JSON, "mean", _RemoteControl_State.Spotmeter.Mean);
        xSemaphoreGive(_RemoteControl_State.Mutex);

        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex for spotmeter read");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t RemoteControl_GetFlashPower(uint8_t *p_Power)
{
    if (p_Power == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get flash power from settings or hardware */
    *p_Power = 50;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_SetFlashPower(uint8_t Power)
{
    if (Power > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Set flash power */
    ESP_LOGI(TAG, "Set flash power to: %u%%", Power);

    return ESP_OK;
}

esp_err_t RemoteControl_GetFlashState(bool *p_Enabled)
{
    if (p_Enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get flash state */
    *p_Enabled = false;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_SetFlashState(bool Enabled)
{
    /* TODO: Set flash state */
    ESP_LOGI(TAG, "Set flash state: %s", Enabled ? "ON" : "OFF");

    return ESP_OK;
}

esp_err_t RemoteControl_GetImageFormat(Remote_Image_Format_t *p_Format)
{
    App_Settings_System_t System;
    esp_err_t Error;

    if (p_Format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = SettingsManager_GetSystem(&System);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Convert from Settings format to Remote format (enum values match) */
    *p_Format = static_cast<Remote_Image_Format_t>(System.ImageFormat);

    return ESP_OK;
}

esp_err_t RemoteControl_SetImageFormat(Remote_Image_Format_t Format)
{
    esp_err_t Error;
    App_Settings_System_t System;
    SettingsManager_Setting_t Changed;

    if ((Format != REMOTE_IMAGE_FORMAT_PNG) &&
        (Format != REMOTE_IMAGE_FORMAT_RAW) &&
        (Format != REMOTE_IMAGE_FORMAT_JPEG)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = SettingsManager_GetSystem(&System);
    if (Error != ESP_OK) {
        return Error;
    }

    Changed.ID = SETTINGS_ID_IMAGE_FORMAT;
    Changed.Value = Format;
    System.ImageFormat = static_cast<App_Settings_Image_Format_t>(Format);

    ESP_LOGI(TAG, "Set image format to: %d", System.ImageFormat);

    return SettingsManager_UpdateSystem(&System, &Changed);
}

esp_err_t RemoteControl_SetStatusLED(Remote_LED_Color_t Color, uint8_t Brightness)
{
    if ((Color != REMOTE_LED_RED) &&
        (Color != REMOTE_LED_GREEN) &&
        (Color != REMOTE_LED_BLUE)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Set LED via PCA9633 driver */
    const char *color_str = (Color == REMOTE_LED_RED) ? "RED" :
                            (Color == REMOTE_LED_GREEN) ? "GREEN" : "BLUE";

    ESP_LOGI(TAG, "Set status LED: %s at brightness %u", color_str, Brightness);

    return ESP_OK;
}

esp_err_t RemoteControl_GetSDCardState(bool *p_Available)
{
    if (p_Available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Check SD card state via MemoryManager */
    *p_Available = false;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_FormatMemory(void)
{
    /* TODO: Format active memory via MemoryManager */
    ESP_LOGW(TAG, "Memory format not implemented");

    return ESP_FAIL;
}

esp_err_t RemoteControl_DisplayMessageBox(const char *p_Message)
{
    if (p_Message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Display message box via GUITask */
    ESP_LOGI(TAG, "Display message: %s", p_Message);

    return ESP_OK;
}

esp_err_t RemoteControl_GetLockState(bool *p_Locked)
{
    if (p_Locked == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Get lock state from settings or GUI */
    *p_Locked = false;  /* Placeholder */

    return ESP_OK;
}

esp_err_t RemoteControl_SetLockState(bool Locked)
{
    /* TODO: Set lock state in settings or GUI */
    ESP_LOGI(TAG, "Set lock state: %s", Locked ? "LOCKED" : "UNLOCKED");

    return ESP_OK;
}
