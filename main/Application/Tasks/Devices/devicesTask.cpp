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
#define DEVICES_TASK_TEMP_CALIBRATION       BIT3
#define DEVICES_TASK_LED_COLOR_R            BIT4    /**< Red channel active in the pending LED request. */
#define DEVICES_TASK_LED_COLOR_G            BIT5    /**< Green channel active in the pending LED request. */
#define DEVICES_TASK_LED_COLOR_B            BIT6    /**< Blue channel active in the pending LED request. */
#define DEVICES_TASK_LED_PATTERN_1x         BIT7    /**< Single flash: 500 ms on. */
#define DEVICES_TASK_LED_PATTERN_2x         BIT8    /**< Double flash: 2 × 250 ms on / 250 ms off. */
#define DEVICES_TASK_LED_PATTERN_3x         BIT9    /**< Triple flash: 3 × 250 ms on / 250 ms off. */
#define DEVICES_TASK_LED_PATTERN_LONG       BIT10   /**< Single long flash: 1000 ms on. */

#define JOYCENTER_LONGPRESS_MS              600

/** @brief EMA smoothing factor for raw TMP117 readings.
 *         At a 5-second poll interval this gives a time constant of approx. 17 s (interval / alpha).
 *         Increase toward 1.0 to react faster; decrease toward 0.0 for heavier smoothing. */
#define TEMP_EMA_ALPHA                      0.3f

/** @brief EMA smoothing factor for the periodic auto-calibration of SensorAtCalibration.
 *         With a 3600-second calibration interval each update moves the baseline 10 % toward the
 *         current filtered reading, giving a drift-tracking time constant of roughly 10 hours. */
#define CAL_EMA_ALPHA                       0.1f

ESP_EVENT_DEFINE_BASE(DEVICES_TASK_EVENTS);

/** @brief LED blink state machine phases used by the DevicesTask LED controller.
 */
typedef enum {
    DEVICES_LED_BLINK_STATE_IDLE,                       /**< No blink in progress; LED is off. */
    DEVICES_LED_BLINK_STATE_ON,                         /**< LED is on; waiting for the on-time to expire. */
    DEVICES_LED_BLINK_STATE_OFF,                        /**< LED is off between flashes; waiting for the off-time to expire. */
} Devices_LED_Blink_State_t;

/** @brief Internal runtime state of the devices task.
 *         Holds FreeRTOS primitives, the latest room temperature reading, and a staging area
 *         for incoming settings-change notifications processed inside the task loop.
 */
typedef struct {
    bool IsInitialized;                                 /**< true after Devices_Task_Init() has completed successfully. */
    bool IsRunning;                                     /**< true while the FreeRTOS task is executing. */
    bool RunTemperatureRead;                            /**< true if a temperature read is requested. */
    bool PrevJoyCenter;                                 /**< Previous JoyCenter state; used for long-press edge detection. */
    bool JoyCenterLongFired;                            /**< true after the long-press event has been posted for the current hold; prevents re-firing. */
    bool LED_R;                                         /**< Red channel of the active blink request. */
    bool LED_G;                                         /**< Green channel of the active blink request. */
    bool LED_B;                                         /**< Blue channel of the active blink request. */
    bool IsEMAInitialized;                              /**< true after the EMA filter has been seeded with the first sensor reading. */
    TaskHandle_t TaskHandle;                            /**< FreeRTOS task handle; NULL before Devices_Task_Start(). */
    EventGroupHandle_t EventGroup;                      /**< Event group used for intra-task synchronisation. */
    SettingsManager_ChangeNotification_t NewSetting;    /**< Staging area for incoming settings-change notifications. */
    int16_t RoomTemperature;                            /**< Latest room temperature in tenths of a degree Celsius. */
    uint8_t LED_CyclesRemaining;                        /**< Number of on/off flash cycles still to execute. */
    uint32_t LED_OnTime_ms;                             /**< Duration of the LED-on phase in milliseconds. */
    uint32_t LED_OffTime_ms;                            /**< Duration of the LED-off phase between flashes in milliseconds. */
    uint32_t CalibrationInterval_s;                     /**< User-configured calibration interval in seconds. */
    float EMA_Temperature;                              /**< EMA-filtered TMP117 reading in °C; 0.0f before the first reading. */
    TickType_t LED_PhaseStart;                          /**< Tick at which the current blink phase started; 0 when idle. */
    TickType_t JoyCenterHeldSince;                      /**< Tick at which JoyCenter went high; 0 when not pressed. */
    Devices_LED_Blink_State_t LED_BlinkState;           /**< Current phase of the LED blink state machine. */
} Devices_Task_State_t;

static Devices_Task_State_t _DevicesTaskState;

static const char *TAG = "Devices-Task";

/** @brief                  Event handler for the GUI task to receive updates when GUI events are triggered (e.g., ROI change requests).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI task event received: ID=%d", ID);

    switch (ID) {
        case GUI_TASK_EVENT_APP_STARTED: {
            _DevicesTaskState.RunTemperatureRead = true;

            xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_LED_COLOR_G | DEVICES_TASK_LED_PATTERN_LONG);

            break;
        }
        case GUI_TASK_EVENT_IMAGE_SAVED: {
            xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_LED_COLOR_G | DEVICES_TASK_LED_PATTERN_1x);

            break;
        }
        case GUI_TASK_EVENT_IMAGE_SAVE_FAILED: {
            xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_LED_COLOR_R | DEVICES_TASK_LED_PATTERN_3x);

            break;
        }
    }
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

                /* The user has entered the current ambient temperature — immediately take a
                 * calibration snapshot to anchor the new offset to the current sensor reading. */
                xEventGroupSetBits(_DevicesTaskState.EventGroup, DEVICES_TASK_TEMP_CALIBRATION);
            } else if (_DevicesTaskState.NewSetting.ID == SETTINGS_ID_CALIBRATION_INTERVAL) {
                _DevicesTaskState.CalibrationInterval_s = static_cast<uint32_t>(_DevicesTaskState.NewSetting.Value);
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
    DevicesManager_Input_State_t InputState;
    DevicesManager_Input_State_t PendingInputState;
    DevicesManager_Battery_Status_t Batttery;
    TickType_t PendingChangeTime;
    TickType_t LastBatteryPoll;
    TickType_t LastTemperaturePoll;
    TickType_t LastCalibration;
    TickType_t LastCalibrationSave;
    bool SDInserted;
    bool HasPendingInput;
    App_Context_t *AppContext;

    esp_task_wdt_add(NULL);

    AppContext = static_cast<App_Context_t *>(p_Parameters);

    ESP_LOGD(TAG, "Devices task started on core %d", xPortGetCoreID());

    memset(&InputState, 0, sizeof(DevicesManager_Input_State_t));
    memset(&PendingInputState, 0, sizeof(DevicesManager_Input_State_t));

    PendingChangeTime = 0;
    HasPendingInput = false;
    LastBatteryPoll = xTaskGetTickCount();
    LastTemperaturePoll = xTaskGetTickCount();
    LastCalibration = xTaskGetTickCount();
    LastCalibrationSave = xTaskGetTickCount();

    /* Report the initial SD-card detect state once at startup. */
    if (DevicesManager_GetSDDetect(&SDInserted) == ESP_OK) {
        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, &SDInserted, sizeof(SDInserted), pdMS_TO_TICKS(100));
    }

    /* Report the initial battery status once at startup. */
    if (DevicesManager_GetBatteryStatus(&Batttery) == ESP_OK) {
        App_Devices_Battery_t NewBatteryInfo = {
            .Voltage = Batttery.Voltage,
            .Percentage = Batttery.Percentage,
            .Charging = Batttery.IsCharging
        };

        esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY,
                       &NewBatteryInfo, sizeof(App_Devices_Battery_t), pdMS_TO_TICKS(100));
    }

    while (_DevicesTaskState.IsRunning) {
        EventBits_t EventBits;
        TickType_t NowTick;

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
            ESP_LOGD(TAG, "Updating display brightness due to settings change");

            DevicesManager_SetBrightness(BACKLIGHT_DISPLAY, _DevicesTaskState.NewSetting.Value);

            xEventGroupClearBits(_DevicesTaskState.EventGroup, DEVICES_TASK_UPDATE_BRIGHTNESS);
        }

        if (EventBits & DEVICES_TASK_TEMP_CALIBRATION) {
            Settings_Calibration_t Calibration;
            float CalTemp = 0.0f;

            /* Prefer the EMA-filtered reading for a noise-free calibration snapshot.
             * Fall back to a fresh raw sensor read if the EMA has not been seeded yet. */
            if (_DevicesTaskState.IsEMAInitialized) {
                CalTemp = _DevicesTaskState.EMA_Temperature;
            } else {
                float RawTemp;

                if (DevicesManager_GetTemperature(&RawTemp) == ESP_OK) {
                    CalTemp = RawTemp;
                }
            }

            if (CalTemp != 0.0f) {
                SettingsManager_GetCalibration(&Calibration);
                Calibration.SensorAtCalibration = CalTemp;
                SettingsManager_UpdateCalibration(&Calibration, NULL);

                ESP_LOGI(TAG, "Manual calibration snapshot: room=%d\xC2\xB0""C, sensor=%.2f\xC2\xB0""C, offset=%.2f\xC2\xB0""C",
                         static_cast<int>(Calibration.RoomTemperature),
                         Calibration.SensorAtCalibration,
                         static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);

                /* Save the updated calibration to persistent storage. */
                SettingsManager_Save();
            }

            xEventGroupClearBits(_DevicesTaskState.EventGroup, DEVICES_TASK_TEMP_CALIBRATION);
        }

        /* Handle a new LED blink request.
         * A request is valid when at least one color bit AND one pattern bit are set simultaneously.
         * A new request always interrupts an ongoing blink sequence. */
        EventBits_t LEDColorBits = EventBits & (DEVICES_TASK_LED_COLOR_R | DEVICES_TASK_LED_COLOR_G | DEVICES_TASK_LED_COLOR_B);
        EventBits_t LEDPatternBits = EventBits & (DEVICES_TASK_LED_PATTERN_1x | DEVICES_TASK_LED_PATTERN_2x |
                                                  DEVICES_TASK_LED_PATTERN_3x | DEVICES_TASK_LED_PATTERN_LONG);

        if ((LEDColorBits != 0) && (LEDPatternBits != 0)) {
            _DevicesTaskState.LED_R = (LEDColorBits & DEVICES_TASK_LED_COLOR_R) == DEVICES_TASK_LED_COLOR_R;
            _DevicesTaskState.LED_G = (LEDColorBits & DEVICES_TASK_LED_COLOR_G) == DEVICES_TASK_LED_COLOR_G;
            _DevicesTaskState.LED_B = (LEDColorBits & DEVICES_TASK_LED_COLOR_B) == DEVICES_TASK_LED_COLOR_B;

            if ((LEDPatternBits & DEVICES_TASK_LED_PATTERN_LONG) == DEVICES_TASK_LED_PATTERN_LONG) {
                _DevicesTaskState.LED_OnTime_ms = 1000;
                _DevicesTaskState.LED_OffTime_ms = 200;
                _DevicesTaskState.LED_CyclesRemaining = 1;
            } else if ((LEDPatternBits & DEVICES_TASK_LED_PATTERN_3x) == DEVICES_TASK_LED_PATTERN_3x) {
                _DevicesTaskState.LED_OnTime_ms = 250;
                _DevicesTaskState.LED_OffTime_ms = 250;
                _DevicesTaskState.LED_CyclesRemaining = 3;
            } else if ((LEDPatternBits & DEVICES_TASK_LED_PATTERN_2x) == DEVICES_TASK_LED_PATTERN_2x) {
                _DevicesTaskState.LED_OnTime_ms = 250;
                _DevicesTaskState.LED_OffTime_ms = 250;
                _DevicesTaskState.LED_CyclesRemaining = 2;
            } else {
                _DevicesTaskState.LED_OnTime_ms = 500;
                _DevicesTaskState.LED_OffTime_ms = 200;
                _DevicesTaskState.LED_CyclesRemaining = 1;
            }

            _DevicesTaskState.LED_BlinkState = DEVICES_LED_BLINK_STATE_ON;
            _DevicesTaskState.LED_PhaseStart = xTaskGetTickCount();
            DevicesManager_SetLED(_DevicesTaskState.LED_R, _DevicesTaskState.LED_G, _DevicesTaskState.LED_B);

            xEventGroupClearBits(_DevicesTaskState.EventGroup, LEDColorBits | LEDPatternBits);
        }

        /* Run the LED blink state machine. */
        if (_DevicesTaskState.LED_BlinkState == DEVICES_LED_BLINK_STATE_ON) {
            if ((xTaskGetTickCount() - _DevicesTaskState.LED_PhaseStart) >= pdMS_TO_TICKS(_DevicesTaskState.LED_OnTime_ms)) {
                DevicesManager_SetLED(false, false, false);
                _DevicesTaskState.LED_CyclesRemaining--;

                if (_DevicesTaskState.LED_CyclesRemaining == 0) {
                    _DevicesTaskState.LED_BlinkState = DEVICES_LED_BLINK_STATE_IDLE;
                    _DevicesTaskState.LED_PhaseStart = 0;
                } else {
                    _DevicesTaskState.LED_BlinkState = DEVICES_LED_BLINK_STATE_OFF;
                    _DevicesTaskState.LED_PhaseStart = xTaskGetTickCount();
                }
            }
        } else if (_DevicesTaskState.LED_BlinkState == DEVICES_LED_BLINK_STATE_OFF) {
            if ((xTaskGetTickCount() - _DevicesTaskState.LED_PhaseStart) >= pdMS_TO_TICKS(_DevicesTaskState.LED_OffTime_ms)) {
                DevicesManager_SetLED(_DevicesTaskState.LED_R, _DevicesTaskState.LED_G, _DevicesTaskState.LED_B);
                _DevicesTaskState.LED_BlinkState = DEVICES_LED_BLINK_STATE_ON;
                _DevicesTaskState.LED_PhaseStart = xTaskGetTickCount();
            }
        }

        if ((_DevicesTaskState.CalibrationInterval_s > 0) &&
            ((xTaskGetTickCount() - LastCalibration) >= pdMS_TO_TICKS(_DevicesTaskState.CalibrationInterval_s * 1000))) {
            LastCalibration = xTaskGetTickCount();

            if (_DevicesTaskState.IsEMAInitialized) {
                Settings_Calibration_t Calibration;

                SettingsManager_GetCalibration(&Calibration);

                /* Gradually blend SensorAtCalibration toward the current EMA temperature.
                 * This tracks long-term device self-heating drift without abrupt jumps.
                 * Each period moves the baseline by CAL_EMA_ALPHA (10 %) of the residual
                 * difference, giving a time constant of roughly interval_s / CAL_EMA_ALPHA. */
                Calibration.SensorAtCalibration = (1.0f - CAL_EMA_ALPHA) * Calibration.SensorAtCalibration +
                                                   CAL_EMA_ALPHA * _DevicesTaskState.EMA_Temperature;
                SettingsManager_UpdateCalibration(&Calibration, NULL);

                /* Persist to NVS at most once every 15 minutes to limit flash wear. */
                if ((xTaskGetTickCount() - LastCalibrationSave) >= pdMS_TO_TICKS(15UL * 60UL * 1000UL)) {
                    LastCalibrationSave = xTaskGetTickCount();
                    SettingsManager_Save();
                }

                ESP_LOGI(TAG,
                         "Auto-calibration drift correction: sensor_baseline=%.2f\xC2\xB0""C, offset=%.2f\xC2\xB0""C",
                         Calibration.SensorAtCalibration,
                         static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);
            }
        }

        if ((xTaskGetTickCount() - LastBatteryPoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_BATTERY_POLL_INTERVAL_S * 1000)) {
            LastBatteryPoll = xTaskGetTickCount();

            if (DevicesManager_GetBatteryStatus(&Batttery) == ESP_OK) {
                App_Devices_Battery_t NewBatteryInfo = {
                    .Voltage = Batttery.Voltage,
                    .Percentage = Batttery.Percentage,
                    .Charging = Batttery.IsCharging
                };

                ESP_LOGD(TAG, "Battery status: %d%%, %d mV, charging=%s", NewBatteryInfo.Percentage, NewBatteryInfo.Voltage,
                         NewBatteryInfo.Charging ? "yes" : "no");

                esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY,
                               &NewBatteryInfo, sizeof(App_Devices_Battery_t), pdMS_TO_TICKS(100));
            }
        }

        if (((xTaskGetTickCount() - LastTemperaturePoll) >= pdMS_TO_TICKS(CONFIG_DEVICES_TASK_TEMPERATURE_POLL_INTERVAL_S *
                                                                          1000)) || _DevicesTaskState.RunTemperatureRead) {
            float Temperature;

            LastTemperaturePoll = xTaskGetTickCount();
            _DevicesTaskState.RunTemperatureRead = false;

            if (DevicesManager_GetTemperature(&Temperature) == ESP_OK) {
                Settings_Calibration_t Calibration;
                App_Devices_Temperature_t NewTemperatureInfo;

                /* Update the EMA filter. Seed with the first reading; apply exponential smoothing
                 * on subsequent readings to suppress I2C noise and short-term fluctuations.
                 * published value = TEMP_EMA_ALPHA * raw + (1 - TEMP_EMA_ALPHA) * previous */
                if (_DevicesTaskState.IsEMAInitialized == false) {
                    _DevicesTaskState.EMA_Temperature = Temperature;
                    _DevicesTaskState.IsEMAInitialized = true;
                } else {
                    _DevicesTaskState.EMA_Temperature = TEMP_EMA_ALPHA * Temperature +
                                                        (1.0f - TEMP_EMA_ALPHA) * _DevicesTaskState.EMA_Temperature;
                }

                SettingsManager_GetCalibration(&Calibration);

                /* Auto-initialize SensorAtCalibration on the first temperature reading.
                 * SensorAtCalibration == 0.0f is the "never calibrated" sentinel.
                 * On first reading: store the current EMA value so the persistent offset
                 * reflects the factory/power-on deviation from the configured room temperature.
                 * offset = RoomTemperature - SensorAtCalibration
                 * estimated_ambient = sensor_current + offset
                 * Example: room = 20, sensor_cal = 24.3 -> offset = -4.3
                 *          sensor_now = 24.5           -> ambient = 24.5 + (-4.3) = 20.2 */
                if (Calibration.SensorAtCalibration == 0.0f) {
                    Calibration.SensorAtCalibration = _DevicesTaskState.EMA_Temperature;
                    SettingsManager_UpdateCalibration(&Calibration, NULL);

                    ESP_LOGI(TAG,
                             "Calibration baseline auto-initialized: sensor = %.2f\xC2\xB0""C, room = %d\xC2\xB0""C, offset = %.2f\xC2\xB0""C",
                             Calibration.SensorAtCalibration,
                             static_cast<int>(Calibration.RoomTemperature),
                             static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);

                    /* Save the updated calibration to persistent storage. */
                    SettingsManager_Save();
                }

                NewTemperatureInfo = {
                    .TempSensor = _DevicesTaskState.EMA_Temperature,
                    .FPA = 0.0f,    /* FPA temperature is not read by the Devices Task; leave it at 0. */
                    .AUX = 0.0f     /* AUX temperature is not read by the Devices Task; leave it at 0. */
                };

                esp_event_post(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE,
                               &NewTemperatureInfo, sizeof(App_Devices_Temperature_t), pdMS_TO_TICKS(100));
            }
        }

        if (DevicesManager_HandleDisplayboardExpanderInterrupt(&InputState) == ESP_OK) {
            if (memcmp(&InputState, &PendingInputState, sizeof(DevicesManager_Input_State_t)) != 0) {
                memcpy(&PendingInputState, &InputState, sizeof(DevicesManager_Input_State_t));
                PendingChangeTime = xTaskGetTickCount();
                HasPendingInput = true;
            }
        }

        /* Commit pending state after 30 ms of stability */
        if (HasPendingInput && ((xTaskGetTickCount() - PendingChangeTime) >= pdMS_TO_TICKS(30))) {
            xSemaphoreTake(AppContext->InputMutex, portMAX_DELAY);
            memcpy(&AppContext->InputState, &PendingInputState, sizeof(DevicesManager_Input_State_t));
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

        /* JoyCenter long-press detection.
         * Tracks raw InputState.JoyCenter and writes the synthetic JoyCenterLongPress field
         * directly into AppContext->InputState (under mutex) so that consumers (e.g. GUI task)
         * can read it via the existing shared input-state mechanism without needing extra events. */
        NowTick = xTaskGetTickCount();

        /* Rising edge: start hold timer. */
        if ((InputState.JoyCenter == true) && (_DevicesTaskState.PrevJoyCenter == false)) {
            _DevicesTaskState.JoyCenterHeldSince = NowTick;
            _DevicesTaskState.JoyCenterLongFired = false;
        }

        /* Long-press threshold crossed: set flag once per hold. */
        if ((InputState.JoyCenter == true) &&
            (_DevicesTaskState.JoyCenterLongFired == false) &&
            (_DevicesTaskState.JoyCenterHeldSince != 0) &&
            ((NowTick - _DevicesTaskState.JoyCenterHeldSince) >= pdMS_TO_TICKS(JOYCENTER_LONGPRESS_MS))) {
            _DevicesTaskState.JoyCenterLongFired = true;

            ESP_LOGD(TAG, "JoyCenter long-press detected");

            xSemaphoreTake(AppContext->InputMutex, portMAX_DELAY);
            AppContext->InputState.JoyCenterLongPress = true;
            xSemaphoreGive(AppContext->InputMutex);
        }

        /* Falling edge: clear the flag so consumers can detect it went low. */
        if ((InputState.JoyCenter == false) && (_DevicesTaskState.PrevJoyCenter == true)) {
            _DevicesTaskState.JoyCenterHeldSince = 0;

            xSemaphoreTake(AppContext->InputMutex, portMAX_DELAY);
            AppContext->InputState.JoyCenterLongPress = false;
            xSemaphoreGive(AppContext->InputMutex);
        }

        _DevicesTaskState.PrevJoyCenter = InputState.JoyCenter;

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
    Settings_Calibration_t Calibration;

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

    esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVED, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_CALIBRATION_CHANGED, on_Settings_Event_Handler, NULL);

    SettingsManager_GetCalibration(&Calibration);
    _DevicesTaskState.CalibrationInterval_s = Calibration.Interval;

    _DevicesTaskState.IsInitialized = true;

    return ESP_OK;
}

void Devices_Task_Deinit(void)
{
    if (_DevicesTaskState.IsInitialized == false) {
        return;
    }

    esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, on_GUI_Task_Event_Handler);
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