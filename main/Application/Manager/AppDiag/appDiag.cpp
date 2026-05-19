/*
 * appDiag.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Global diagnostics / error-capture system implementation.
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

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string.h>

#include "appDiag.h"

static const char *TAG = "AppDiag";

/** @brief Names of the diagnostic source identifiers, indexed by AppDiag_Source_t.
 */
static const char *const _Source_Names[APP_DIAG_SOURCE_COUNT] = {
    "Devices",
    "Memory",
    "Network",
    "Settings",
    "Time",
    "USB",
    "Application",
    "LeptonTask",
    "NetworkTask",
    "DevicesTask",
    "GUITask",
    "CameraTask",
};

/** @brief Internal state of the diagnostics ring buffer.
 */
typedef struct {
    AppDiag_Entry_t Entries[APP_DIAG_MAX_ENTRIES];  /**< Ring buffer storage. */
    uint32_t Head;                                  /**< Index of the next write slot (wraps at APP_DIAG_MAX_ENTRIES). */
    uint32_t Count;                                 /**< Number of valid entries currently stored (max APP_DIAG_MAX_ENTRIES). */
    SemaphoreHandle_t Mutex;                        /**< FreeRTOS mutex; NULL before AppDiag_Init(). */
    bool IsInitialized;                             /**< true after AppDiag_Init() has succeeded. */
} AppDiag_State_t;

static AppDiag_State_t _AppDiag_State;

esp_err_t AppDiag_Init(void)
{
    if (_AppDiag_State.IsInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    _AppDiag_State.Mutex = xSemaphoreCreateMutex();
    if (_AppDiag_State.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");

        return ESP_ERR_NO_MEM;
    }

    _AppDiag_State.IsInitialized = true;

    ESP_LOGD(TAG, "Diagnostics module initialised (ring buffer: %u entries)", APP_DIAG_MAX_ENTRIES);

    return ESP_OK;
}

void AppDiag_RecordError(AppDiag_Source_t Source, esp_err_t ErrorCode, const char *p_Context)
{
    AppDiag_Entry_t *p_Entry;
    bool TakeMutex = (_AppDiag_State.IsInitialized && (_AppDiag_State.Mutex != NULL));

    if (TakeMutex) {
        xSemaphoreTake(_AppDiag_State.Mutex, portMAX_DELAY);
    }

    p_Entry = &_AppDiag_State.Entries[_AppDiag_State.Head];
    p_Entry->TimestampUs = esp_timer_get_time();
    p_Entry->Source      = Source;
    p_Entry->ErrorCode   = ErrorCode;

    if (p_Context != NULL) {
        strncpy(p_Entry->Context, p_Context, APP_DIAG_CONTEXT_LEN - 1);
        p_Entry->Context[APP_DIAG_CONTEXT_LEN - 1] = '\0';
    } else {
        p_Entry->Context[0] = '\0';
    }

    _AppDiag_State.Head = (_AppDiag_State.Head + 1) % APP_DIAG_MAX_ENTRIES;

    if (_AppDiag_State.Count < APP_DIAG_MAX_ENTRIES) {
        _AppDiag_State.Count++;
    }

    if (TakeMutex) {
        xSemaphoreGive(_AppDiag_State.Mutex);
    }

    /* Log the entry so it is visible on the monitor regardless of display state */
    const char *SourceName = (Source < APP_DIAG_SOURCE_COUNT) ? _Source_Names[Source] : "?";
    ESP_LOGW(TAG, "[%s] Error 0x%04X (%s)", SourceName, static_cast<unsigned int>(ErrorCode),
             (p_Context != NULL) ? p_Context : "");
}

uint32_t AppDiag_GetCount(void)
{
    uint32_t Count;

    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreTake(_AppDiag_State.Mutex, portMAX_DELAY);
    }

    Count = _AppDiag_State.Count;

    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreGive(_AppDiag_State.Mutex);
    }

    return Count;
}

bool AppDiag_HasErrors(void)
{
    return (AppDiag_GetCount() > 0);
}

esp_err_t AppDiag_GetEntry(uint32_t Index, AppDiag_Entry_t *p_Entry)
{
    uint32_t RealIndex;

    if (p_Entry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreTake(_AppDiag_State.Mutex, portMAX_DELAY);
    }

    if (Index >= _AppDiag_State.Count) {
        if (_AppDiag_State.Mutex != NULL) {
            xSemaphoreGive(_AppDiag_State.Mutex);
        }

        return ESP_ERR_NOT_FOUND;
    }

    /* Translate logical index (0 = oldest) into physical ring-buffer position.
     * When Count < MAX_ENTRIES the oldest entry is at slot 0;
     * when the buffer is full the oldest entry is at Head (the next write slot). */
    if (_AppDiag_State.Count < APP_DIAG_MAX_ENTRIES) {
        RealIndex = Index;
    } else {
        RealIndex = (_AppDiag_State.Head + Index) % APP_DIAG_MAX_ENTRIES;
    }

    *p_Entry = _AppDiag_State.Entries[RealIndex];

    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreGive(_AppDiag_State.Mutex);
    }

    return ESP_OK;
}

void AppDiag_Clear(void)
{
    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreTake(_AppDiag_State.Mutex, portMAX_DELAY);
    }

    __builtin_memset(_AppDiag_State.Entries, 0, sizeof(_AppDiag_State.Entries));
    _AppDiag_State.Head  = 0;
    _AppDiag_State.Count = 0;

    if (_AppDiag_State.Mutex != NULL) {
        xSemaphoreGive(_AppDiag_State.Mutex);
    }

    ESP_LOGD(TAG, "Diagnostics buffer cleared");
}
