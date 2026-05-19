/*
 * http_handler_telemetry.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for telemetry.
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

#include "http_handler.h"
#include "../http_server.h"
#include "../../../../Devices/devicesManager.h"
#include "../../../../Network/networkManager.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Telemetry-Handler";

esp_err_t HTTP_Handler_Telemetry(httpd_req_t *p_Request)
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