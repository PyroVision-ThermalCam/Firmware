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
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include <string.h>
#include <stdbool.h>

#include "devicesTask.h"
#include "Application/application.h"
#include "Application/Manager/Devices/devicesManager.h"

#define DEVICES_TASK_STOP_REQUEST           BIT0
#define DEVICES_TASK_TIME_SYNCED            BIT1
#define DEVICES_TASK_UPDATE_BRIGHTNESS      BIT2

ESP_EVENT_DEFINE_BASE(DEVICES_TASK_EVENTS);

typedef struct {
    bool isInitialized;
    bool isRunning;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    SettingsManager_ChangeNotification_t NewSetting;
} Devices_Task_State_t;

static Devices_Task_State_t _Devices_Task_State;

static const char *TAG = "Devices-Task";

/** @brief                  GUI task event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI task event received: ID=%d", ID);
}

/** @brief                  Event handler for the Settings task to receive updates when settings are changed.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Settings_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Settings event received: ID=%d", ID);

    switch (ID) {
        case SETTINGS_EVENT_DISPLAY_CHANGED: {
            memcpy(&_Devices_Task_State.NewSetting, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGI(TAG, "Display settings changed: ID=%d", _Devices_Task_State.NewSetting.ID);
            ESP_LOGI(TAG, "Display settings changed: Value=%d", _Devices_Task_State.NewSetting.Value);

            if (_Devices_Task_State.NewSetting.ID == SETTINGS_ID_DISPLAY_BRIGHTNESS) {
                xEventGroupSetBits(_Devices_Task_State.EventGroup, DEVICES_TASK_UPDATE_BRIGHTNESS);
            }

            break;
        }
    }
}

/** @brief              Devices task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Devices(void *p_Parameters)
{
    Devices_InputState_t InputState;
    Devices_InputState_t PrevInputState;
    TickType_t LastInputPoll;
    TickType_t LastBatteryPoll;
    TickType_t LastTemperaturePoll;

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Devices task started on core %d", xPortGetCoreID());

    memset(&InputState, 0, sizeof(Devices_InputState_t));
    memset(&PrevInputState, 0, sizeof(Devices_InputState_t));

    LastInputPoll = xTaskGetTickCount();
    LastBatteryPoll = xTaskGetTickCount();
    LastTemperaturePoll = xTaskGetTickCount();

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

        if (EventBits & DEVICES_TASK_UPDATE_BRIGHTNESS) {
            ESP_LOGI(TAG, "Updating display brightness due to settings change");

            DevicesManager_SetBrightness(BACKLIGHT_DISPLAY, _Devices_Task_State.NewSetting.Value);

            xEventGroupClearBits(_Devices_Task_State.EventGroup, DEVICES_TASK_UPDATE_BRIGHTNESS);
        }

        if (DevicesManager_AcquireI2CBus(pdMS_TO_TICKS(500)) == ESP_OK) {
            DevicesManager_HandleExpanderInterrupt();

            if ((xTaskGetTickCount() - LastInputPoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_INPUT_POLL_INTERVAL_MS)) {
                LastInputPoll = xTaskGetTickCount();

                if (DevicesManager_GetDisplayboardInputs(&InputState) == ESP_OK) {
                    if (memcmp(&InputState, &PrevInputState, sizeof(Devices_InputState_t)) != 0) {
                        ESP_LOGI(TAG, "Input: Joy[U=%d D=%d L=%d R=%d C=%d] Btn[1=%d 2=%d 3=%d 4=%d]",
                                 static_cast<int>(InputState.JoyUp),
                                 static_cast<int>(InputState.JoyDown),
                                 static_cast<int>(InputState.JoyLeft),
                                 static_cast<int>(InputState.JoyRight),
                                 static_cast<int>(InputState.JoyCenter),
                                 static_cast<int>(InputState.Button1),
                                 static_cast<int>(InputState.Button2),
                                 static_cast<int>(InputState.Button3),
                                 static_cast<int>(InputState.Button4));

                        esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_INPUT_CHANGED,
                                       &InputState, sizeof(Devices_InputState_t), pdMS_TO_TICKS(100));

                        memcpy(&PrevInputState, &InputState, sizeof(Devices_InputState_t));
                    }
                }
            }
  
            if ((xTaskGetTickCount() - LastBatteryPoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_BATTERY_POLL_INTERVAL_S * 1000)) {
                int Voltage;
                uint8_t Percentage;
                bool Charging;

                LastBatteryPoll = xTaskGetTickCount();

                if (DevicesManager_GetBatteryStatus(&Voltage, &Percentage, &Charging) == ESP_OK) {
                    App_Devices_Battery_t NewBatteryInfo = {
                        .Voltage = Voltage,
                        .Percentage = Percentage,
                        .Charging = Charging
                    };

                    esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY,
                                   &NewBatteryInfo, sizeof(App_Devices_Battery_t), pdMS_TO_TICKS(100));
                }
            }

            if ((xTaskGetTickCount() - LastTemperaturePoll) >= pdMS_TO_TICKS(1000)) {
                float Temperature;

                LastTemperaturePoll = xTaskGetTickCount();

                if (DevicesManager_GetTemperature(&Temperature) == ESP_OK) {
                    App_Devices_Temperature_t NewTemperatureInfo = {
                        .Temperature = Temperature
                    };

                    esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE,
                                   &NewTemperatureInfo, sizeof(App_Devices_Temperature_t), pdMS_TO_TICKS(100));
                }
            }

            DevicesManager_ReleaseI2CBus();
        } 

        vTaskDelay(pdMS_TO_TICKS(10));
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

    esp_event_handler_register(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, on_Settings_Event_Handler, NULL);

    _Devices_Task_State.isInitialized = true;

    return ESP_OK;
}

void Devices_Task_Deinit(void)
{
    if (_Devices_Task_State.isInitialized == false) {
        return;
    }

    esp_event_handler_unregister(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, on_Settings_Event_Handler);

    DevicesManager_Deinit();

    _Devices_Task_State.isInitialized = false;

    return;
}

esp_err_t Devices_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

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

    Error = xTaskCreatePinnedToCore(Task_Devices, "Task_Devices", CONFIG_DEVICES_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_DEVICES_TASK_PRIO, &_Devices_Task_State.TaskHandle, CONFIG_DEVICES_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Devices Task: 0x%X!", Error);

        _Devices_Task_State.isRunning = false;

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