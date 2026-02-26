/*
 * cameraTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Camera task implementation.
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
#include <esp_event.h>
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string.h>
#include <stdbool.h>

#include "cameraTask.h"
#include "Application/application.h"

#define CAMERA_TASK_STOP_REQUEST           BIT0

ESP_EVENT_DEFINE_BASE(CAMERA_EVENTS);

typedef struct {
    bool isInitialized;
    bool isRunning;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
} Camera_Task_State_t;

static Camera_Task_State_t _Camera_Task_State;

static const char *TAG = "cameraTask";

/** @brief              Camera task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Camera(void *p_Parameters)
{
    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Camera task started on core %d", xPortGetCoreID());

    while (_Camera_Task_State.isRunning) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "Camera task shutting down");
    //CameraManager_Deinit();

    _Camera_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Camera_Task_Init(void)
{
    esp_err_t Error;

    Error = ESP_OK;

    if (_Camera_Task_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    _Camera_Task_State.EventGroup = xEventGroupCreate();
    if (_Camera_Task_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    //Error = CameraManager_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Camera Manager: 0x%x!", Error);

        return Error;
    }

    _Camera_Task_State.isInitialized = true;

    return ESP_OK;
}

void Camera_Task_Deinit(void)
{
    if (_Camera_Task_State.isInitialized == false) {
        return;
    }

    //CameraManager_Deinit();

    if (_Camera_Task_State.EventGroup != NULL) {
        vEventGroupDelete(_Camera_Task_State.EventGroup);
        _Camera_Task_State.EventGroup = NULL;
    }

    _Camera_Task_State.isInitialized = false;

    return;
}

esp_err_t Camera_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Camera_Task_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_Camera_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    _Camera_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Camera Task");

    Error = xTaskCreatePinnedToCore(Task_Camera, "Task_Camera", CONFIG_CAMERA_TASK_STACKSIZE, p_AppContext, CONFIG_CAMERA_TASK_PRIO, &_Camera_Task_State.TaskHandle, CONFIG_CAMERA_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Camera Task: %d!", Error);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Camera_Task_Stop(void)
{
    if (_Camera_Task_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Camera Task");

    xEventGroupSetBits(_Camera_Task_State.EventGroup, CAMERA_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Camera_Task_IsRunning(void)
{
    return _Camera_Task_State.isRunning;
}