/*
 * devicesTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Devices Task implementation.
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

#include "devicesTask.h"
#include "Application/application.h"
#include "Application/Manager/Time/timeTypes.h"

#define DEVICES_TASK_STOP_REQUEST           BIT0
#define DEVICES_TASK_TIME_SYNCED            BIT1

ESP_EVENT_DEFINE_BASE(DEVICE_EVENTS);

typedef struct {
    bool isInitialized;
    bool isRunning;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint32_t LastBatteryUpdate;
    uint32_t LastTimeUpdate;
    struct timeval TimeOfDay;
} Devices_Task_State_t;

static Devices_Task_State_t _Devices_Task_State;

static const char *TAG = "Devices-Task";

/** @brief                  Event handler for GUI events.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI event received: ID=%d", ID);
}

/** @brief              Devices task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Devices(void *p_Parameters)
{
    uint32_t Now;

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Devices task started on core %d", xPortGetCoreID());

    while (_Devices_Task_State.isRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_Devices_Task_State.EventGroup);
        if (EventBits & DEVICES_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _Devices_Task_State.isRunning = false;

            xEventGroupClearBits(_Devices_Task_State.EventGroup, DEVICES_TASK_STOP_REQUEST);

            break;
        }

        Now = esp_timer_get_time() / 1000;

        if ((Now - _Devices_Task_State.LastBatteryUpdate) >= 1000) {
            App_Devices_Battery_t BatteryInfo;

            ESP_LOGD(TAG, "Updating battery voltage...");

            if (DevicesManager_GetBatteryVoltage(&BatteryInfo.Voltage, &BatteryInfo.Percentage) == ESP_OK) {
                esp_event_post(DEVICE_EVENTS, DEVICE_EVENT_RESPONSE_BATTERY_VOLTAGE, &BatteryInfo, sizeof(BatteryInfo), portMAX_DELAY);
            } else {
                ESP_LOGE(TAG, "Failed to read battery voltage!");
            }

            _Devices_Task_State.LastBatteryUpdate = esp_timer_get_time() / 1000;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Devices task shutting down");
    DevicesManager_Deinit();

    _Devices_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Devices_Task_Init(void)
{
    esp_err_t Error;

    if (_Devices_Task_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    /* Create event group */
    _Devices_Task_State.EventGroup = xEventGroupCreate();
    if (_Devices_Task_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    Error = DevicesManager_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Devices Manager: 0x%x!", Error);

        return Error;
    }

    /* Use the event loop to receive control signals from other tasks */
    esp_event_handler_register(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler, NULL);

    _Devices_Task_State.isInitialized = true;

    return ESP_OK;
}

void Devices_Task_Deinit(void)
{
    if (_Devices_Task_State.isInitialized == false) {
        return;
    }

    esp_event_handler_unregister(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler);

    DevicesManager_Deinit();

    if (_Devices_Task_State.EventGroup != NULL) {
        vEventGroupDelete(_Devices_Task_State.EventGroup);
        _Devices_Task_State.EventGroup = NULL;
    }

    _Devices_Task_State.isInitialized = false;

    return;
}

esp_err_t Devices_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t ret;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Devices_Task_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_Devices_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already running");

        return ESP_OK;
    }

    _Devices_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Devices Task");

    ret = xTaskCreatePinnedToCore(
              Task_Devices,
              "Task_Devices",
              CONFIG_DEVICES_TASK_STACKSIZE,
              p_AppContext,
              CONFIG_DEVICES_TASK_PRIO,
              &_Devices_Task_State.TaskHandle,
              CONFIG_DEVICES_TASK_CORE
          );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Devices Task: %d!", ret);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Devices_Task_Stop(void)
{
    if (_Devices_Task_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Devices Task");

    xEventGroupSetBits(_Devices_Task_State.EventGroup, DEVICES_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Devices_Task_IsRunning(void)
{
    return _Devices_Task_State.isRunning;
}