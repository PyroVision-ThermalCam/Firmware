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
#include "AppDiag/appDiag.h"

#define DEVICES_TASK_STOP_REQUEST           BIT0
#define DEVICES_TASK_TIME_SYNCED            BIT1
#define DEVICES_TASK_UPDATE_BRIGHTNESS      BIT2
#define DEVICES_TASK_CALIBRATION_CHANGED    BIT3

ESP_EVENT_DEFINE_BASE(DEVICES_TASK_EVENTS);

/** @brief Internal runtime state of the devices task.
 *         Holds FreeRTOS primitives, the latest room temperature reading, and a staging area
 *         for incoming settings-change notifications processed inside the task loop.
 */
typedef struct {
    bool IsInitialized;                                 /**< true after Devices_Task_Init() has completed successfully. */
    bool IsRunning;                                     /**< true while the FreeRTOS task is executing. */
    bool RunCalibration;                                /**< true if a calibration run is requested. */
    TaskHandle_t TaskHandle;                            /**< FreeRTOS task handle; NULL before Devices_Task_Start(). */
    EventGroupHandle_t EventGroup;                      /**< Event group used for intra-task synchronisation. */
    SettingsManager_ChangeNotification_t NewSetting;    /**< Staging area for incoming settings-change notifications. */
    int16_t RoomTemperature;                            /**< Latest room temperature in tenths of a degree Celsius. */
} Devices_Task_State_t;

static Devices_Task_State_t _DevicesTaskState;

static const char *TAG = "Devices-Task";

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
            memcpy(&_DevicesTaskState.NewSetting, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGD(TAG, "Display settings changed: ID=%d", _DevicesTaskState.NewSetting.ID);
            ESP_LOGD(TAG, "Display settings changed: Value=%d", _DevicesTaskState.NewSetting.Value);

            if (_DevicesTaskState.NewSetting.ID == SETTINGS_ID_DISPLAY_BRIGHTNESS) {
                xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_UPDATE_BRIGHTNESS);
            }

            break;
        }
        case SETTINGS_EVENT_CALIBRATION_CHANGED: {
            /* p_Data is NULL when LeptonTask stores the calibration snapshot internally -
             * the RoomTemperature has not changed in that case, so nothing to update. */
            if (p_Data == NULL) {
                break;
            }

            memcpy(&_DevicesTaskState.NewSetting, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGD(TAG, "Calibration settings changed: ID=%d", _DevicesTaskState.NewSetting.ID);
            ESP_LOGD(TAG, "Calibration settings changed: Value=%d", _DevicesTaskState.NewSetting.Value);

            if (_DevicesTaskState.NewSetting.ID == SETTINGS_ID_CALIBRATION_ROOM_TEMP) {
                _DevicesTaskState.RoomTemperature = static_cast<int16_t>(_DevicesTaskState.NewSetting.Value);
            }

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled settings event ID: 0x%X", ID);

            break;
        }
    }
}

/** @brief              Devices task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Devices(void *p_Parameters)
{
    Devices_Input_State_t InputState;
    Devices_Input_State_t PendingInputState;
    TickType_t PendingChangeTime;
    TickType_t LastBatteryPoll;
    TickType_t LastTemperaturePoll;
    int Voltage;
    uint8_t Percentage;
    bool Charging;
    bool SDInserted;
    bool HasPendingInput;
    App_Context_t *AppContext;

    esp_task_wdt_add(NULL);

    AppContext = static_cast<App_Context_t *>(p_Parameters);

    ESP_LOGD(TAG, "Devices task started on core %d", xPortGetCoreID());

    memset(&InputState, 0, sizeof(Devices_Input_State_t));
    memset(&PendingInputState, 0, sizeof(Devices_Input_State_t));

    PendingChangeTime = 0;
    HasPendingInput = false;
    LastBatteryPoll = xTaskGetTickCount();
    LastTemperaturePoll = xTaskGetTickCount();

    /* Report the initial SD-card detect state once at startup. */
    if (DevicesManager_GetSDDetect(&SDInserted) == ESP_OK) {
        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, &SDInserted, sizeof(SDInserted), pdMS_TO_TICKS(100));
    }

    /* Report the initial battery status once at startup. */
    if (DevicesManager_GetBatteryStatus(&Voltage, &Percentage, &Charging) == ESP_OK) {
        App_Devices_Battery_t NewBatteryInfo = {
            .Voltage = Voltage,
            .Percentage = Percentage,
            .Charging = Charging
        };

        esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY,
                       &NewBatteryInfo, sizeof(App_Devices_Battery_t), pdMS_TO_TICKS(100));
    }

    while (_DevicesTaskState.IsRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        DevicesManager_HandleExpanderInterrupt();

        EventBits = xEventGroupGetBits(_DevicesTaskState.EventGroup);
        if (EventBits & DEVICES_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _DevicesTaskState.IsRunning = false;

            xEventGroupClearBits(_DevicesTaskState.EventGroup, DEVICES_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & DEVICES_TASK_UPDATE_BRIGHTNESS) {
            ESP_LOGI(TAG, "Updating display brightness due to settings change");

            DevicesManager_SetBrightness(BACKLIGHT_DISPLAY, _DevicesTaskState.NewSetting.Value);

            xEventGroupClearBits(_DevicesTaskState.EventGroup, DEVICES_TASK_UPDATE_BRIGHTNESS);
        }

        if (EventBits & DEVICES_TASK_CALIBRATION_CHANGED) {
            float SensorTemp;
            Settings_Calibration_t Calibration;

            SettingsManager_GetCalibration(&Calibration);

            /* Record the sensor reading at the moment of calibration.
             * From this point on: offset = RoomTemperature - SensorAtCalibration.
             */
            if (DevicesManager_GetTemperature(&SensorTemp) == ESP_OK) {
                Calibration.SensorAtCalibration = SensorTemp;
                SettingsManager_UpdateCalibration(&Calibration, NULL);

                ESP_LOGI(TAG, "Calibration snapshot: room=%d\xC2\xB0""C, sensor=%.2f\xC2\xB0""C, offset=%.2f\xC2\xB0""C",
                         static_cast<int>(Calibration.RoomTemperature),
                         Calibration.SensorAtCalibration,
                         static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);
            }

            xEventGroupClearBits(_DevicesTaskState.EventGroup, DEVICES_TASK_CALIBRATION_CHANGED);
        }

        if ((xTaskGetTickCount() - LastBatteryPoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_BATTERY_POLL_INTERVAL_S * 1000)) {
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

        if ((xTaskGetTickCount() - LastTemperaturePoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_TEMPERATURE_POLL_INTERVAL_S *
                                                                         1000)) {
            float Temperature;

            LastTemperaturePoll = xTaskGetTickCount();

            if (DevicesManager_GetTemperature(&Temperature) == ESP_OK) {
                Settings_Calibration_t Calibration;
                App_Devices_Temperature_t NewTemperatureInfo;

                SettingsManager_GetCalibration(&Calibration);

                /* Auto-initialize SensorAtCalibration on the first temperature reading.
                * SensorAtCalibration == 0.0f is the "never calibrated" sentinel.
                * On first reading: store the current sensor value so the persistent offset
                * reflects the factory/power-on deviation from the configured room temperature.
                * offset = RoomTemperature - SensorAtCalibration
                * estimated_ambient = sensor_current + offset
                * Example: room=20, sensor_cal=24.3 -> offset=-4.3
                *          sensor_now=24.5           -> ambient=24.5+(-4.3)=20.2 */
                if ((Calibration.SensorAtCalibration == 0.0f) || _DevicesTaskState.RunCalibration) {
                    Calibration.SensorAtCalibration = Temperature;
                    SettingsManager_UpdateCalibration(&Calibration, NULL);

                    ESP_LOGI(TAG,
                             "Calibration baseline auto-initialized: sensor = %.2f\xC2\xB0""C, room = %d\xC2\xB0""C, offset = %.2f\xC2\xB0""C",
                             Calibration.SensorAtCalibration,
                             static_cast<int>(Calibration.RoomTemperature),
                             static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);

                    /* Save the updated calibration to persistent storage. */
                    SettingsManager_Save();

                    _DevicesTaskState.RunCalibration = false;
                }

                NewTemperatureInfo = {
                    .TempSensor = Temperature,
                };

                esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE,
                               &NewTemperatureInfo, sizeof(App_Devices_Temperature_t), pdMS_TO_TICKS(100));
            }
        }

        if (DevicesManager_HandleDisplayboardExpanderInterrupt(&InputState) == ESP_OK) {
            if (memcmp(&InputState, &PendingInputState, sizeof(Devices_Input_State_t)) != 0) {
                memcpy(&PendingInputState, &InputState, sizeof(Devices_Input_State_t));
                PendingChangeTime = xTaskGetTickCount();
                HasPendingInput = true;
            }
        }

        /* Commit pending state after 30 ms of stability */
        if (HasPendingInput && ((xTaskGetTickCount() - PendingChangeTime) >= pdMS_TO_TICKS(30))) {
            xSemaphoreTake(AppContext->InputMutex, portMAX_DELAY);
            memcpy(&AppContext->InputState, &PendingInputState, sizeof(Devices_Input_State_t));
            xSemaphoreGive(AppContext->InputMutex);

            HasPendingInput = false;

            ESP_LOGD(TAG, "Input committed: Joy[U=%d D=%d L=%d R=%d C=%d] Btn[1=%d 2=%d 3=%d 4=%d]",
                     static_cast<int>(PendingInputState.JoyUp),
                     static_cast<int>(PendingInputState.JoyDown),
                     static_cast<int>(PendingInputState.JoyLeft),
                     static_cast<int>(PendingInputState.JoyRight),
                     static_cast<int>(PendingInputState.JoyCenter),
                     static_cast<int>(PendingInputState.Button1),
                     static_cast<int>(PendingInputState.Button2),
                     static_cast<int>(PendingInputState.Button3),
                     static_cast<int>(PendingInputState.Button4));
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "Devices task shutting down");

    DevicesManager_Deinit();

    _DevicesTaskState.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Devices_Task_Init(void)
{
    esp_err_t Error;

    if (_DevicesTaskState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    _DevicesTaskState.EventGroup = xEventGroupCreate();
    if (_DevicesTaskState.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_DEVICES, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    Error = DevicesManager_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Devices Manager: 0x%x!", Error);

        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_DEVICES, Error);

        return Error;
    }

    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_CALIBRATION_CHANGED, on_Settings_Event_Handler, NULL);

    _DevicesTaskState.RunCalibration = true;
    _DevicesTaskState.IsInitialized = true;

    return ESP_OK;
}

void Devices_Task_Deinit(void)
{
    if (_DevicesTaskState.IsInitialized == false) {
        return;
    }

    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, on_Settings_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_CALIBRATION_CHANGED, on_Settings_Event_Handler);

    DevicesManager_Deinit();

    _DevicesTaskState.IsInitialized = false;

    return;
}

esp_err_t Devices_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_DevicesTaskState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_DevicesTaskState.IsRunning) {
        ESP_LOGW(TAG, "Task already running");

        return ESP_OK;
    }

    _DevicesTaskState.IsRunning = true;

    ESP_LOGD(TAG, "Starting Devices Task");

    Error = xTaskCreatePinnedToCore(Task_Devices, "Task_Devices", CONFIG_DEVICES_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_DEVICES_TASK_PRIO, &_DevicesTaskState.TaskHandle, CONFIG_DEVICES_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Devices Task: 0x%X!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_DEVICES, ESP_ERR_NO_MEM);

        _DevicesTaskState.IsRunning = false;

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Devices_Task_Stop(void)
{
    if (_DevicesTaskState.IsRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Devices Task");

    xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Devices_Task_IsRunning(void)
{
    return _DevicesTaskState.IsRunning;
}