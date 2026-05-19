/*
 * websocketRemoteHandlers.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WebSocket handlers for remote control interface.
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

#include <string>
#include <cstring>

#include "websocketRemoteHandlers.h"
#include "../../RemoteControl/remoteControl.h"

static const char *TAG = "WebSocket-Remote";

static void WebSocket_SendResponse(int FD, const char *p_Cmd, const char *p_Status, cJSON *p_Data, const char *p_Error)
{
    cJSON *Response;

    Response = cJSON_CreateObject();
    cJSON_AddStringToObject(Response, "status", p_Status);

    if (p_Data != NULL) {
        cJSON_AddItemReferenceToObject(Response, "data", p_Data);
    } else if (p_Error != NULL) {
        cJSON_AddStringToObject(Response, "error", p_Error);
    }

    WebSocket_SendJSON(FD, p_Cmd, Response);
    cJSON_Delete(Response);
}

void WebSocket_Handle_GetTemperature(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    float Temperature;
    cJSON *Data;

    Error = RemoteControl_GetTemperature(&Temperature);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "temperature", "error", NULL, "Failed to get temperature");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddNumberToObject(Data, "temperature", Temperature);

    WebSocket_SendResponse(FD, "temperature", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_GetTime(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    char TimeStr[32];
    cJSON *Data;

    Error = RemoteControl_GetTime(TimeStr, sizeof(TimeStr));
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "time", "error", NULL, "Failed to get time");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddStringToObject(Data, "time", TimeStr);

    WebSocket_SendResponse(FD, "time", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetTime(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *TimeField;

    TimeField = cJSON_GetObjectItem(p_Data, "time");
    if (cJSON_IsString(TimeField) == false) {
        WebSocket_SendResponse(FD, "set_time", "error", NULL, "Missing or invalid 'time' field");

        return;
    }

    Error = RemoteControl_SetTime(TimeField->valuestring);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_time", "error", NULL, "Invalid time format");

        return;
    }

    WebSocket_SendResponse(FD, "set_time", "ok", NULL, NULL);
}

void WebSocket_Handle_GetBattery(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    int Voltage;
    uint8_t SOC;
    bool Charging;
    cJSON *Data;

    Error = RemoteControl_GetBatteryStatus(&Voltage, &SOC, &Charging);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "battery", "error", NULL, "Failed to get battery voltage");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddNumberToObject(Data, "voltage", Voltage);
    cJSON_AddNumberToObject(Data, "soc", SOC);
    cJSON_AddBoolToObject(Data, "charging", Charging);

    WebSocket_SendResponse(FD, "battery", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_GetLeptonEmissivity(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *Data;
    uint8_t Emissivity;

    Error = RemoteControl_GetLeptonEmissivity(&Emissivity);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "lepton_emissivity", "error", NULL, "Failed to get emissivity");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddNumberToObject(Data, "emissivity", Emissivity);

    WebSocket_SendResponse(FD, "lepton_emissivity", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetLeptonEmissivity(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *EmisField;

    EmisField = cJSON_GetObjectItem(p_Data, "emissivity");
    if (cJSON_IsNumber(EmisField) == false) {
        WebSocket_SendResponse(FD, "set_lepton_emissivity", "error", NULL, "Missing or invalid 'emissivity' field");

        return;
    }

    Error = RemoteControl_SetLeptonEmissivity(static_cast<uint8_t>(EmisField->valueint));
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_lepton_emissivity", "error", NULL, "Invalid emissivity value");

        return;
    }

    WebSocket_SendResponse(FD, "set_lepton_emissivity", "ok", NULL, NULL);
}

void WebSocket_Handle_GetLeptonStats(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *Data;

    Data = cJSON_CreateObject();
    Error = RemoteControl_GetLeptonSceneStats(Data);
    if (Error != ESP_OK) {
        cJSON_Delete(Data);
        WebSocket_SendResponse(FD, "lepton_stats", "error", NULL, "Failed to get scene stats");

        return;
    }

    WebSocket_SendResponse(FD, "lepton_stats", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_GetLeptonROI(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *Data;

    Data = cJSON_CreateObject();
    Error = RemoteControl_GetLeptonROI(Data);
    if (Error != ESP_OK) {
        cJSON_Delete(Data);
        WebSocket_SendResponse(FD, "lepton_roi", "error", NULL, "Failed to get ROI");
        return;
    }

    WebSocket_SendResponse(FD, "lepton_roi", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetLeptonROI(int FD, cJSON *p_Data)
{
    esp_err_t Error;

    Error = RemoteControl_SetLeptonROI(p_Data);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_lepton_roi", "error", NULL, "Invalid ROI data");

        return;
    }

    WebSocket_SendResponse(FD, "set_lepton_roi", "ok", NULL, NULL);
}

void WebSocket_Handle_GetLeptonSpotmeter(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *Data;

    Data = cJSON_CreateObject();
    Error = RemoteControl_GetLeptonSpotmeter(Data);
    if (Error != ESP_OK) {
        cJSON_Delete(Data);
        WebSocket_SendResponse(FD, "lepton_spotmeter", "error", NULL, "Failed to get spotmeter");

        return;
    }

    WebSocket_SendResponse(FD, "lepton_spotmeter", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_GetFlash(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *Data;
    bool Enabled;
    uint8_t Power;

    Error = RemoteControl_GetFlashConfig(&Enabled, &Power);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "flash", "error", NULL, "Failed to get flash state");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddBoolToObject(Data, "enabled", Enabled);
    cJSON_AddNumberToObject(Data, "power", Power);

    WebSocket_SendResponse(FD, "flash", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetFlash(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *EnabledField;
    cJSON *PowerField;

    EnabledField = cJSON_GetObjectItem(p_Data, "enabled");
    if (cJSON_IsBool(EnabledField)) {
        Error = RemoteControl_SetFlashState(cJSON_IsTrue(EnabledField));
        if (Error != ESP_OK) {
            WebSocket_SendResponse(FD, "set_flash", "error", NULL, "Failed to set flash state");

            return;
        }
    }

    PowerField = cJSON_GetObjectItem(p_Data, "power");
    if (cJSON_IsNumber(PowerField)) {
        Error = RemoteControl_SetFlashPower(static_cast<uint8_t>(PowerField->valueint));
        if (Error != ESP_OK) {
            WebSocket_SendResponse(FD, "set_flash", "error", NULL, "Invalid flash power value");

            return;
        }
    }

    WebSocket_SendResponse(FD, "set_flash", "ok", NULL, NULL);
}

void WebSocket_Handle_GetImageFormat(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    ImageEncoder_Format_t ImageFormat;
    const char *Format;
    cJSON *Data;

    Error = RemoteControl_GetImageFormat(&ImageFormat);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "image_format", "error", NULL, "Failed to get image format");

        return;
    }

    switch (ImageFormat) {
        case IMAGE_FORMAT_PNG: {
            Format = "PNG";

            break;
        }
        case IMAGE_FORMAT_RAW: {
            Format = "RAW";

            break;
        }
        case IMAGE_FORMAT_JPEG: {
            Format = "JPEG";

            break;
        }
        default: {
            WebSocket_SendResponse(FD, "image_format", "error", NULL, "Unknown format");

            return;
        }
    }

    Data = cJSON_CreateObject();
    cJSON_AddStringToObject(Data, "format", Format);

    WebSocket_SendResponse(FD, "image_format", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetImageFormat(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *FormatField;
    ImageEncoder_Format_t Format;

    FormatField = cJSON_GetObjectItem(p_Data, "format");
    if (cJSON_IsString(FormatField) == false) {
        WebSocket_SendResponse(FD, "set_image_format", "error", NULL, "Missing or invalid 'format' field");
        return;
    }

    if (strcasecmp(FormatField->valuestring, "PNG") == 0) {
        Format = IMAGE_FORMAT_PNG;
    } else if (strcasecmp(FormatField->valuestring, "RAW") == 0) {
        Format = IMAGE_FORMAT_RAW;
    } else if (strcasecmp(FormatField->valuestring, "JPEG") == 0) {
        Format = IMAGE_FORMAT_JPEG;
    } else {
        WebSocket_SendResponse(FD, "set_image_format", "error", NULL, "Invalid format");
        return;
    }

    Error = RemoteControl_SetImageFormat(Format);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_image_format", "error", NULL, "Failed to set image format");
        return;
    }

    WebSocket_SendResponse(FD, "set_image_format", "ok", NULL, NULL);
}

void WebSocket_Handle_SetStatusLED(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *ColorField;
    cJSON *BrightnessField;
    Remote_LED_Color_t Color;
    uint8_t Brightness;

    ColorField = cJSON_GetObjectItem(p_Data, "color");
    BrightnessField = cJSON_GetObjectItem(p_Data, "brightness");

    if ((cJSON_IsString(ColorField) == false) || (cJSON_IsNumber(BrightnessField) == false)) {
        WebSocket_SendResponse(FD, "set_status_led", "error", NULL, "Missing or invalid fields");

        return;
    }

    if (strcasecmp(ColorField->valuestring, "RED") == 0) {
        Color = REMOTE_LED_RED;
    } else if (strcasecmp(ColorField->valuestring, "GREEN") == 0) {
        Color = REMOTE_LED_GREEN;
    } else if (strcasecmp(ColorField->valuestring, "BLUE") == 0) {
        Color = REMOTE_LED_BLUE;
    } else {
        WebSocket_SendResponse(FD, "set_status_led", "error", NULL, "Invalid color");
        return;
    }

    Brightness = static_cast<uint8_t>(BrightnessField->valueint);

    Error = RemoteControl_SetStatusLED(Color, Brightness);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_status_led", "error", NULL, "Failed to set LED");
        return;
    }

    WebSocket_SendResponse(FD, "set_status_led", "ok", NULL, NULL);
}

void WebSocket_Handle_GetSDState(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    bool Available;
    cJSON *Data;

    Error = RemoteControl_GetSDCardState(&Available);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "sd_state", "error", NULL, "Failed to get SD state");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddBoolToObject(Data, "available", Available);

    WebSocket_SendResponse(FD, "sd_state", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_FormatMemory(int FD, cJSON *p_Data)
{
    esp_err_t Error;

    Error = RemoteControl_FormatMemory();
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "format_memory", "error", NULL, "Format operation failed");

        return;
    }

    WebSocket_SendResponse(FD, "format_memory", "ok", NULL, NULL);
}

void WebSocket_Handle_DisplayMessage(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *MessageField;

    MessageField = cJSON_GetObjectItem(p_Data, "message");
    if (cJSON_IsString(MessageField) == false) {
        WebSocket_SendResponse(FD, "display_message", "error", NULL, "Missing or invalid 'message' field");

        return;
    }

    Error = RemoteControl_DisplayMessageBox(MessageField->valuestring);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "display_message", "error", NULL, "Failed to display message");

        return;
    }

    WebSocket_SendResponse(FD, "display_message", "ok", NULL, NULL);
}

void WebSocket_Handle_GetLock(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    bool Locked;
    cJSON *Data;

    Error = RemoteControl_GetLockState(&Locked);
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "lock", "error", NULL, "Failed to get lock state");

        return;
    }

    Data = cJSON_CreateObject();
    cJSON_AddBoolToObject(Data, "locked", Locked);

    WebSocket_SendResponse(FD, "lock", "ok", Data, NULL);
    cJSON_Delete(Data);
}

void WebSocket_Handle_SetLock(int FD, cJSON *p_Data)
{
    esp_err_t Error;
    cJSON *LockedField;

    LockedField = cJSON_GetObjectItem(p_Data, "locked");
    if (cJSON_IsBool(LockedField) == false) {
        WebSocket_SendResponse(FD, "set_lock", "error", NULL, "Missing or invalid 'locked' field");

        return;
    }

    Error = RemoteControl_SetLockState(cJSON_IsTrue(LockedField));
    if (Error != ESP_OK) {
        WebSocket_SendResponse(FD, "set_lock", "error", NULL, "Failed to set lock state");

        return;
    }

    WebSocket_SendResponse(FD, "set_lock", "ok", NULL, NULL);
}
