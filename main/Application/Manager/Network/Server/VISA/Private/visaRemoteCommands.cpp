/*
 * visaRemoteCommands.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA commands for remote control interface.
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
#include <stdio.h>
#include <string.h>
#include <cJSON.h>

#include "visaRemoteCommands.h"
#include "visaCommands.h"
#include "../../RemoteControl/remoteControl.h"

static const char *TAG = "VISA-Remote";

int VISA_Cmd_GetTemperature(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    float Temperature;

    Error = RemoteControl_GetTemperature(&Temperature);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%.2f\n", Temperature);
}

int VISA_Cmd_GetTime(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    char TimeStr[32];

    Error = RemoteControl_GetTime(TimeStr, sizeof(TimeStr));
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%s\n", TimeStr);
}

int VISA_Cmd_SetTime(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    Error = RemoteControl_SetTime(pp_Tokens[3]);
    if (Error != ESP_OK) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    return 0;
}

int VISA_Cmd_GetBatteryVoltage(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    float Voltage;

    Error = RemoteControl_GetBatteryVoltage(&Voltage);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%.3f\n", Voltage);
}

int VISA_Cmd_GetStateOfCharge(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    uint8_t SOC;

    Error = RemoteControl_GetStateOfCharge(&SOC);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%u\n", SOC);
}

int VISA_Cmd_GetLeptonEmissivity(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    uint8_t Emissivity;

    Error = RemoteControl_GetLeptonEmissivity(&Emissivity);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%u\n", Emissivity);
}

int VISA_Cmd_SetLeptonEmissivity(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    int Emissivity;

    if (Count < 5) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    Emissivity = atoi(pp_Tokens[4]);
    if ((Emissivity < 0) || (Emissivity > 100)) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetLeptonEmissivity((uint8_t)Emissivity);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetLeptonStats(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    cJSON *JSON;
    char *JSONStr;
    int len;

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    Error = RemoteControl_GetLeptonSceneStats(JSON);
    if (Error != ESP_OK) {
        cJSON_Delete(JSON);
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    JSONStr = cJSON_PrintUnformatted(JSON);
    cJSON_Delete(JSON);

    if (JSONStr == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    len = snprintf(p_Response, MaxLen, "%s\n", JSONStr);
    cJSON_free(JSONStr);

    return len;
}

int VISA_Cmd_GetLeptonROI(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    cJSON *JSON;
    char *JSONStr;
    int len;

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    Error = RemoteControl_GetLeptonROI(JSON);
    if (Error != ESP_OK) {
        cJSON_Delete(JSON);
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    JSONStr = cJSON_PrintUnformatted(JSON);
    cJSON_Delete(JSON);

    if (JSONStr == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    len = snprintf(p_Response, MaxLen, "%s\n", JSONStr);
    cJSON_free(JSONStr);

    return len;
}

int VISA_Cmd_SetLeptonROI(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    cJSON *JSON;

    if (Count < 5) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    /* Expected format: SENS:IMG:LEP:ROI x,y,width,height */
    /* Parse as JSON: {"x":40,"y":30,"width":80,"height":60} */
    JSON = cJSON_Parse(pp_Tokens[4]);
    if (JSON == NULL) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetLeptonROI(JSON);
    cJSON_Delete(JSON);

    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetLeptonSpotmeter(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    cJSON *JSON;
    char *JSONStr;
    int len;

    JSON = cJSON_CreateObject();
    if (JSON == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    Error = RemoteControl_GetLeptonSpotmeter(JSON);
    if (Error != ESP_OK) {
        cJSON_Delete(JSON);
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    JSONStr = cJSON_PrintUnformatted(JSON);
    cJSON_Delete(JSON);

    if (JSONStr == NULL) {
        return SCPI_ERROR_OUT_OF_MEMORY;
    }

    len = snprintf(p_Response, MaxLen, "%s\n", JSONStr);
    cJSON_free(JSONStr);

    return len;
}

int VISA_Cmd_GetFlashPower(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    uint8_t Power;

    Error = RemoteControl_GetFlashPower(&Power);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%u\n", Power);
}

int VISA_Cmd_SetFlashPower(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    int Power;

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    Power = atoi(pp_Tokens[3]);
    if ((Power < 0) || (Power > 100)) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetFlashPower((uint8_t)Power);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetFlashState(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Enabled;

    Error = RemoteControl_GetFlashState(&Enabled);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%s\n", Enabled ? "ON" : "OFF");
}

int VISA_Cmd_SetFlashState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Enabled;

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    if ((strcasecmp(pp_Tokens[3], "ON") == 0) || (strcasecmp(pp_Tokens[3], "1") == 0)) {
        Enabled = true;
    } else if ((strcasecmp(pp_Tokens[3], "OFF") == 0) || (strcasecmp(pp_Tokens[3], "0") == 0)) {
        Enabled = false;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetFlashState(Enabled);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetImageFormat(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    Remote_Image_Format_t Format;
    const char *FormatStr;

    Error = RemoteControl_GetImageFormat(&Format);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    switch (Format) {
        case REMOTE_IMAGE_FORMAT_PNG:
            FormatStr = "PNG";
            break;
        case REMOTE_IMAGE_FORMAT_RAW:
            FormatStr = "RAW";
            break;
        case REMOTE_IMAGE_FORMAT_JPEG:
            FormatStr = "JPEG";
            break;
        default:
            return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%s\n", FormatStr);
}

int VISA_Cmd_SetImageFormat(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    Remote_Image_Format_t Format;

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    if (strcasecmp(pp_Tokens[3], "PNG") == 0) {
        Format = REMOTE_IMAGE_FORMAT_PNG;
    } else if (strcasecmp(pp_Tokens[3], "RAW") == 0) {
        Format = REMOTE_IMAGE_FORMAT_RAW;
    } else if (strcasecmp(pp_Tokens[3], "JPEG") == 0) {
        Format = REMOTE_IMAGE_FORMAT_JPEG;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetImageFormat(Format);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_SetStatusLED(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    Remote_LED_Color_t Color;
    int Brightness;

    if (Count < 5) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    if (strcasecmp(pp_Tokens[3], "RED") == 0) {
        Color = REMOTE_LED_RED;
    } else if (strcasecmp(pp_Tokens[3], "GREEN") == 0) {
        Color = REMOTE_LED_GREEN;
    } else if (strcasecmp(pp_Tokens[3], "BLUE") == 0) {
        Color = REMOTE_LED_BLUE;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Brightness = atoi(pp_Tokens[4]);
    if ((Brightness < 0) || (Brightness > 255)) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetStatusLED(Color, (uint8_t)Brightness);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetSDCardState(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Available;

    Error = RemoteControl_GetSDCardState(&Available);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%s\n", Available ? "AVAILABLE" : "NOT_AVAILABLE");
}

int VISA_Cmd_FormatMemory(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;

    Error = RemoteControl_FormatMemory();
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_DisplayMessageBox(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    Error = RemoteControl_DisplayMessageBox(pp_Tokens[3]);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetLockState(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Locked;

    Error = RemoteControl_GetLockState(&Locked);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    return snprintf(p_Response, MaxLen, "%s\n", Locked ? "LOCKED" : "UNLOCKED");
}

int VISA_Cmd_SetLockState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Locked;

    if (Count < 3) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    if ((strcasecmp(pp_Tokens[2], "LOCKED") == 0) || (strcasecmp(pp_Tokens[2], "1") == 0)) {
        Locked = true;
    } else if ((strcasecmp(pp_Tokens[2], "UNLOCKED") == 0) || (strcasecmp(pp_Tokens[2], "0") == 0)) {
        Locked = false;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetLockState(Locked);
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}
