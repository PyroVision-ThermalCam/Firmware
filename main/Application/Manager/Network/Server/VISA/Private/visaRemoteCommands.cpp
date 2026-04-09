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

#include <stdio.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cJSON.h>

#include "visaRemoteCommands.h"
#include "visaCommands.h"
#include "../../RemoteControl/remoteControl.h"
#include "../../../../Devices/devicesManager.h"

int VISA_Cmd_GetTemperature(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    float Temperature;
    std::string Response;
    std::string TempStr;

    Error = RemoteControl_GetTemperature(&Temperature);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    TempStr = std::to_string(Temperature);
    Response = TempStr.substr(0, TempStr.find('.') + 3) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_GetTime(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    char TimeStr[32];
    std::string Response;

    Error = RemoteControl_GetTime(TimeStr, sizeof(TimeStr));
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    Response = std::string(TimeStr) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
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
    int Voltage;
    uint8_t SOC;
    std::string Response;
    std::string VoltageStr;

    Error = RemoteControl_GetBatteryVoltage(&Voltage, &SOC);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    VoltageStr = std::to_string(Voltage);
    Response = VoltageStr.substr(0, VoltageStr.find('.') + 4) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_GetLeptonEmissivity(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    std::string Response;
    uint8_t Emissivity;

    Error = RemoteControl_GetLeptonEmissivity(&Emissivity);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    Response = std::to_string(Emissivity) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
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

    Error = RemoteControl_SetLeptonEmissivity(static_cast<uint8_t>(Emissivity));
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
    std::string Response;

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

    Response = std::string(JSONStr) + "\n";
    cJSON_free(JSONStr);
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_GetLeptonROI(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    cJSON *JSON;
    char *JSONStr;
    std::string Response;

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

    Response = std::string(JSONStr) + "\n";
    cJSON_free(JSONStr);
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
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
    std::string Response;

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

    Response = std::string(JSONStr) + "\n";
    cJSON_free(JSONStr);
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_GetFlashPower(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    std::string Response;
    bool Enabled;
    uint8_t Power;

    Error = RemoteControl_GetFlashConfig(&Enabled, &Power);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    Response = std::to_string(Power) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
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

    Error = RemoteControl_SetFlashPower(static_cast<uint8_t>(Power));
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetFlashState(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    std::string Response;
    bool Enabled;
    uint8_t Power;

    Error = RemoteControl_GetFlashConfig(&Enabled, &Power);
    if (Error != ESP_OK) {
        return Error;
    }

    Response = std::string(Enabled ? "ON" : "OFF") + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_SetFlashState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Enabled;
    std::string Value(pp_Tokens[3]);

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    std::transform(Value.begin(), Value.end(), Value.begin(), ::tolower);
    if ((Value == "on") || (Value == "1")) {
        Enabled = true;
    } else if ((Value == "off") || (Value == "0")) {
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
    Settings_Image_Format_t Format;
    const char *FormatStr;
    std::string Response;

    Error = RemoteControl_GetImageFormat(&Format);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    switch (Format) {
        case IMAGE_FORMAT_PNG:
            FormatStr = "PNG";
            break;
        case IMAGE_FORMAT_RAW:
            FormatStr = "RAW";
            break;
        case IMAGE_FORMAT_JPEG:
            FormatStr = "JPEG";
            break;
        default:
            return SCPI_ERROR_HARDWARE_ERROR;
    }

    Response = std::string(FormatStr) + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_SetImageFormat(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    Settings_Image_Format_t Format;
    std::string FormatValue(pp_Tokens[3]);

    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    std::transform(FormatValue.begin(), FormatValue.end(), FormatValue.begin(), ::toupper);
    if (FormatValue == "PNG") {
        Format = IMAGE_FORMAT_PNG;
    } else if (FormatValue == "RAW") {
        Format = IMAGE_FORMAT_RAW;
    } else if (FormatValue == "JPEG") {
        Format = IMAGE_FORMAT_JPEG;
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
    std::string ColorValue(pp_Tokens[3]);

    if (Count < 5) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    std::transform(ColorValue.begin(), ColorValue.end(), ColorValue.begin(), ::toupper);
    if (ColorValue == "RED") {
        Color = REMOTE_LED_RED;
    } else if (ColorValue == "GREEN") {
        Color = REMOTE_LED_GREEN;
    } else if (ColorValue == "BLUE") {
        Color = REMOTE_LED_BLUE;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Brightness = atoi(pp_Tokens[4]);
    if ((Brightness < 0) || (Brightness > 255)) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    Error = RemoteControl_SetStatusLED(Color, static_cast<uint8_t>(Brightness));
    if (Error != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}

int VISA_Cmd_GetSDCardState(char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Available;
    std::string Response;

    Error = RemoteControl_GetSDCardState(&Available);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    Response = std::string(Available ? "AVAILABLE" : "NOT_AVAILABLE") + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
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
    std::string Response;

    Error = RemoteControl_GetLockState(&Locked);
    if (Error != ESP_OK) {
        return SCPI_ERROR_HARDWARE_ERROR;
    }

    Response = std::string(Locked ? "LOCKED" : "UNLOCKED") + "\n";
    strncpy(p_Response, Response.c_str(), MaxLen);

    return Response.size();
}

int VISA_Cmd_SetLockState(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    esp_err_t Error;
    bool Locked;
    std::string LockValue(pp_Tokens[2]);

    if (Count < 3) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    std::transform(LockValue.begin(), LockValue.end(), LockValue.begin(), ::toupper);
    if ((LockValue == "LOCKED") || (LockValue == "1")) {
        Locked = true;
    } else if ((LockValue == "UNLOCKED") || (LockValue == "0")) {
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

int VISA_Cmd_SetImagePalette(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    std::string PaletteValue(pp_Tokens[3]);
    std::transform(PaletteValue.begin(), PaletteValue.end(), PaletteValue.begin(), ::toupper);

    /* TODO: Implement palette setting via RemoteControl interface */
    /* Valid: IRON, GRAY, RAINBOW */
    if ((PaletteValue == "IRON") || (PaletteValue == "GRAY") || (PaletteValue == "RAINBOW")) {
        return 0;
    } else {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }
}

int VISA_Cmd_SetLEDBrightness(char **pp_Tokens, int Count, char *p_Response, size_t MaxLen)
{
    if (Count < 4) {
        return SCPI_ERROR_MISSING_PARAMETER;
    }

    int Brightness = atoi(pp_Tokens[3]);
    if ((Brightness < 0) || (Brightness > 255)) {
        return SCPI_ERROR_DATA_OUT_OF_RANGE;
    }

    if (DevicesManager_SetLEDBrightness(static_cast<uint8_t>(Brightness)) != ESP_OK) {
        return SCPI_ERROR_EXECUTION_ERROR;
    }

    return 0;
}
