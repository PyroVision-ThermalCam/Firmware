/*
 * http_handler_memory.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for memory.
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
#include "../../../../Memory/memoryManager.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Memory-Handler";

esp_err_t HTTP_Handler_Memory(httpd_req_t *p_Request)
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
