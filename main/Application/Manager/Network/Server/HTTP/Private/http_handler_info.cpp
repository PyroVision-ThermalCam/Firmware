/*
 * http_handler_info.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for info.
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

#include "http_handler.h"
#include "../http_server.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Info-Handler";

esp_err_t HTTP_Handler_Info(httpd_req_t *p_Request)
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