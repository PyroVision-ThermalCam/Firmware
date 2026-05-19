/*
 * http_handler_settings.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for settings.
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
#include "../Manager/Settings/settingsManager.h"
#include "../../application/app_types.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Settings-Handler";

esp_err_t HTTP_Handler_Settings(httpd_req_t *p_Request)
{
    esp_err_t Error;
    cJSON *JSON;
    cJSON *Item;
    cJSON *Group;
    SettingsManager_ChangeNotification_t Changed;
    int PaletteIdx;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    }

    JSON = HTTP_Server_ParseJSON(p_Request);
    if (JSON == NULL) {
        return HTTP_Server_SendError(p_Request, 400, "Invalid JSON");
    }

    Item = cJSON_GetObjectItem(JSON, "palette");
    if (Item != NULL) {
        if (cJSON_IsNumber(Item) == false) {
            cJSON_Delete(JSON);

            return HTTP_Server_SendError(p_Request, 400, "Invalid 'palette' field");
        }

        PaletteIdx = Item->valueint;
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

    Group = cJSON_GetObjectItem(JSON, "camera");
    if (Group != NULL) {
        int Index;

        Item = cJSON_GetObjectItem(Group, "index");
        if ((Item != NULL) && cJSON_IsNumber(Item)) {
            Index = Item->valueint;

            ESP_LOGI(TAG, "Switching camera view: Index -> %u", Index);

            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_SWITCH_CAMERA, &Index, sizeof(int), pdMS_TO_TICKS(100));
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

