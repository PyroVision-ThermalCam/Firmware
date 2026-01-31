/*
 * leptonTask.c
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Main lepton task implementation.
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
#include <esp_event.h>
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string.h>
#include <stdbool.h>

#include "lepton.h"
#include "leptonTask.h"
#include "Application/application.h"

#define LEPTON_TASK_STOP_REQUEST                BIT0
#define LEPTON_TASK_UPDATE_ROI_REQUEST          BIT1
#define LEPTON_TASK_UPDATE_TEMP_REQUEST         BIT2
#define LEPTON_TASK_UPDATE_UPTIME_REQUEST       BIT3
#define LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE    BIT4
#define LEPTON_TASK_UPDATE_SPOTMETER            BIT5
#define LEPTON_TASK_UPDATE_SCENE_STATISTICS     BIT6
#define LEPTON_TASK_UPDATE_EMISSIVITY           BIT7

ESP_EVENT_DEFINE_BASE(LEPTON_EVENTS);

typedef struct {
    bool isInitialized;
    bool Running;
    bool RunTask;
    bool ApplicationStarted;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint8_t *RGB_Buffer[2];
    uint8_t CurrentReadBuffer;
    uint16_t Emissivity;
    SemaphoreHandle_t BufferMutex;
    QueueHandle_t RawFrameQueue;
    Lepton_FrameBuffer_t RawFrame;
    Lepton_Conf_t LeptonConf;
    Lepton_t Lepton;
    App_Settings_ROI_t ROI;
    App_GUI_Screenposition_t ScreenPosition;
} Lepton_Task_State_t;

static Lepton_Task_State_t _LeptonTask_State;

static const char *TAG = "lepton_task";

static void on_GUI_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI event received: ID=%d", ID);

    switch (ID) {
        case GUI_EVENT_APP_STARTED: {
            ESP_LOGD(TAG, "Application started event received");

            _LeptonTask_State.ApplicationStarted = true;

            break;
        }
        case GUI_EVENT_REQUEST_ROI: {
            memcpy(&_LeptonTask_State.ROI, p_Data, sizeof(App_Settings_ROI_t));

            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);

            break;
        }
        case GUI_EVENT_REQUEST_FPA_AUX_TEMP: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
            break;
        }
        case GUI_EVENT_REQUEST_UPTIME: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);

            break;
        }
        case GUI_EVENT_REQUEST_PIXEL_TEMPERATURE: {
            _LeptonTask_State.ScreenPosition = *(App_GUI_Screenposition_t *)p_Data;

            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

            break;
        }
        case GUI_EVENT_REQUEST_SPOTMETER: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SPOTMETER);

            break;
        }
        case GUI_EVENT_REQUEST_SCENE_STATISTICS: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);

            break;
        }
        case GUI_EVENT_REQUEST_EMISSIVITY: {
            _LeptonTask_State.Emissivity = *(uint16_t *)p_Data;

            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled GUI event ID: %d", ID);
            break;
        }
    }
}

/** @brief              Lepton camera task main loop.
 *  @param p_Parameters Task parameters
 */
static void Task_Lepton(void *p_Parameters)
{
    Lepton_Error_t Lepton_Error;
    App_Context_t *App_Context;
    App_Lepton_Device_t DeviceInfo;

    esp_task_wdt_add(NULL);
    App_Context = reinterpret_cast<App_Context_t *>(p_Parameters);

    ESP_LOGD(TAG, "Lepton task started on core %d", xPortGetCoreID());

    /* Initialize Lepton (this creates I2C device handle) before framebuffer allocation */
    Lepton_Error = Lepton_Init(&_LeptonTask_State.Lepton, &_LeptonTask_State.LeptonConf);
    if (Lepton_Error != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Lepton initialization failed with error: %d!", Lepton_Error);
        esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_CAMERA_ERROR, NULL, 0, portMAX_DELAY);

        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
    }

    /* Format serial number as readable string: XXXX-XXXX-XXXX-XXXX */
    snprintf(DeviceInfo.SerialNumber, sizeof(DeviceInfo.SerialNumber),
             "%02X%02X-%02X%02X-%02X%02X-%02X%02X",
             _LeptonTask_State.Lepton.SerialNumber[0], _LeptonTask_State.Lepton.SerialNumber[1],
             _LeptonTask_State.Lepton.SerialNumber[2], _LeptonTask_State.Lepton.SerialNumber[3],
             _LeptonTask_State.Lepton.SerialNumber[4], _LeptonTask_State.Lepton.SerialNumber[5],
             _LeptonTask_State.Lepton.SerialNumber[6], _LeptonTask_State.Lepton.SerialNumber[7]);
    memcpy(DeviceInfo.PartNumber, _LeptonTask_State.Lepton.PartNumber, sizeof(DeviceInfo.PartNumber));

    ESP_LOGD(TAG, "	Part number: %s", DeviceInfo.PartNumber);
    ESP_LOGD(TAG, "	Serial number: %s", DeviceInfo.SerialNumber);

    esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_CAMERA_READY, &DeviceInfo, sizeof(App_Lepton_Device_t), portMAX_DELAY);

    while (_LeptonTask_State.ApplicationStarted == false) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    Lepton_FluxLinearParams_t FluxParams;
    Lepton_GetFluxLinearParameters(&_LeptonTask_State.Lepton, &FluxParams);
    ESP_LOGI(TAG,
             "Flux Linear Parameters - Scene Emissivity: %u, TBkgK: %u, TauWindow: %u, TWindowK: %u, TauAtm: %u, TAtmK: %u, ReflWindow: %u, TReflK: %u",
             FluxParams.SceneEmissivity,
             FluxParams.TBkgK,
             FluxParams.TauWindow,
             FluxParams.TWindowK,
             FluxParams.TauAtm,
             FluxParams.TAtmK,
             FluxParams.ReflWindow,
             FluxParams.TReflK);

    ESP_LOGD(TAG, "Start image capturing...");

    if (Lepton_StartCapture(&_LeptonTask_State.Lepton, _LeptonTask_State.RawFrameQueue) != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Can not start image capturing!");
        esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_CAMERA_ERROR, NULL, 0, portMAX_DELAY);

        /* Critical error - cannot continue without capture task */
        _LeptonTask_State.Running = false;
        _LeptonTask_State.TaskHandle = NULL;

        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);

        return;
    }

    _LeptonTask_State.RunTask = true;
    while (_LeptonTask_State.RunTask) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Wait for a new raw frame with longer timeout to avoid busy waiting */
        if (xQueueReceive(_LeptonTask_State.RawFrameQueue, &_LeptonTask_State.RawFrame, 500 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t WriteBufferIdx;
            uint8_t *WriteBuffer;
            Lepton_Telemetry_t Telemetry;
            Lepton_VideoFormat_t VideoFormat;
            int16_t Min = 0;
            int16_t Max = 0;

            if (_LeptonTask_State.RawFrame.Telemetry_Buffer != NULL) {
                memcpy(&Telemetry, _LeptonTask_State.RawFrame.Telemetry_Buffer, sizeof(Lepton_Telemetry_t));
                ESP_LOGD(TAG, "Telemetry - FrameCounter: %u, FPA_Temp: %uK, Housing_Temp: %uK",
                         Telemetry.FrameCounter,
                         Telemetry.FPA_Temp,
                         Telemetry.Housing_Temp);
            }

            ESP_LOGD(TAG, "Processing frame...");

            /* Determine which buffer to write to (ping-pong) */
            if (xSemaphoreTake(_LeptonTask_State.BufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                /* Find a buffer that's not currently being read */
                WriteBufferIdx = (_LeptonTask_State.CurrentReadBuffer + 1) % 2;
                WriteBuffer = _LeptonTask_State.RGB_Buffer[WriteBufferIdx];
                xSemaphoreGive(_LeptonTask_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer selection!");
                continue;
            }

            /* Process frame based on video format */
            Lepton_GetVideoFormat(&_LeptonTask_State.Lepton, &VideoFormat);

            if (VideoFormat == LEPTON_FORMAT_RGB888) {
                /* RGB888: Data is already in RGB format, just copy it */
                size_t ImageSize = _LeptonTask_State.RawFrame.Width * _LeptonTask_State.RawFrame.Height *
                                   _LeptonTask_State.RawFrame.BytesPerPixel;
                memcpy(WriteBuffer, _LeptonTask_State.RawFrame.Image_Buffer, ImageSize);
                ESP_LOGD(TAG, "Copied RGB888 frame: %ux%u (%u bytes)", _LeptonTask_State.RawFrame.Width,
                         _LeptonTask_State.RawFrame.Height, static_cast<unsigned int>(ImageSize));
            } else {
                /* RAW14: Convert to RGB */
                Lepton_Raw14ToRGB(&_LeptonTask_State.Lepton, _LeptonTask_State.RawFrame.Image_Buffer, WriteBuffer, &Min, &Max,
                                  _LeptonTask_State.RawFrame.Width,
                                  _LeptonTask_State.RawFrame.Height);
            }

            /* Mark buffer as ready and update read buffer index */
            if (xSemaphoreTake(_LeptonTask_State.BufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                _LeptonTask_State.CurrentReadBuffer = WriteBufferIdx;
                xSemaphoreGive(_LeptonTask_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer ready!");
                continue;
            }

            /* Send frame notification to network task */
            App_Lepton_FrameReady_t FrameEvent = {
                .Buffer = WriteBuffer,
                .Width = _LeptonTask_State.RawFrame.Width,
                .Height = _LeptonTask_State.RawFrame.Height,
                .Channels = 3,
                .Min = Min,
                .Max = Max
            };

            /* Use xQueueOverwrite to always have the latest frame */
            xQueueOverwrite(App_Context->Lepton_FrameEventQueue, &FrameEvent);
            ESP_LOGD(TAG, "Frame sent to queue successfully");
        } else {
            /* Timeout waiting for frame */
            ESP_LOGW(TAG, "No raw frame received from VoSPI");
        }

        EventBits = xEventGroupGetBits(_LeptonTask_State.EventGroup);
        if (EventBits & LEPTON_TASK_UPDATE_ROI_REQUEST) {
            Lepton_ROI_t ROI;
            Lepton_Error_t Error;

            ROI.Start_Col = _LeptonTask_State.ROI.x;
            ROI.Start_Row = _LeptonTask_State.ROI.y;
            ROI.End_Col = _LeptonTask_State.ROI.x + _LeptonTask_State.ROI.w - 1;
            ROI.End_Row = _LeptonTask_State.ROI.y + _LeptonTask_State.ROI.h - 1;

            switch (_LeptonTask_State.ROI.Type) {
                case ROI_TYPE_SPOTMETER: {
                    Error = Lepton_SetSpotmeterROI(&_LeptonTask_State.Lepton, &ROI);
                    break;
                }
                case ROI_TYPE_SCENE: {
                    Error = Lepton_SetSceneROI(&_LeptonTask_State.Lepton, &ROI);
                    break;
                }
                case ROI_TYPE_AGC: {
                    Error = Lepton_SetAGCROI(&_LeptonTask_State.Lepton, &ROI);
                    break;
                }
                case ROI_TYPE_VIDEO_FOCUS: {
                    Error = Lepton_SetVideoFocusROI(&_LeptonTask_State.Lepton, &ROI);
                    break;
                }
                default: {
                    ESP_LOGW(TAG, "Invalid ROI type in GUI event: %d", _LeptonTask_State.ROI.Type);
                    return;
                }
            }

            if (Error == LEPTON_ERR_OK) {
                ESP_LOGD(TAG, "New Lepton ROI (Type %d) - Start_Col: %u, Start_Row: %u, End_Col: %u, End_Row: %u",
                         _LeptonTask_State.ROI.Type,
                         ROI.Start_Col,
                         ROI.Start_Row,
                         ROI.End_Col,
                         ROI.End_Row);
            } else {
                ESP_LOGE(TAG, "Failed to update Lepton ROI with type %d!", _LeptonTask_State.ROI.Type);
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);
        } else if (EventBits & LEPTON_TASK_UPDATE_TEMP_REQUEST) {
            uint16_t FPA_Temp;
            uint16_t AUX_Temp;
            App_Lepton_Temperatures_t Temperatures;

            Lepton_GetTemperature(&_LeptonTask_State.Lepton, &FPA_Temp, &AUX_Temp);

            Temperatures.FPA = (static_cast<float>(FPA_Temp) * 0.01f) - 273.0f;
            Temperatures.AUX = (static_cast<float>(AUX_Temp) * 0.01f) - 273.0f;

            esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_FPA_AUX_TEMP, &Temperatures, sizeof(App_Lepton_Temperatures_t), 0);

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
        } else if (EventBits & LEPTON_TASK_UPDATE_UPTIME_REQUEST) {
            uint32_t Uptime;

            Uptime = Lepton_GetUptime(&_LeptonTask_State.Lepton);

            esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_UPTIME, &Uptime, sizeof(uint32_t), 0);

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);
        } else if (EventBits & LEPTON_TASK_STOP_REQUEST) {
            ESP_LOGI(TAG, "Stop request received");

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

            break;
        } else if (EventBits & LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE) {
            int16_t x;
            int16_t y;
            float Temperature;
            Lepton_VideoFormat_t VideoFormat;

            Lepton_GetVideoFormat(&_LeptonTask_State.Lepton, &VideoFormat);
            if (((_LeptonTask_State.RawFrame.Width == 0) || (_LeptonTask_State.RawFrame.Height == 0)) &&
                (VideoFormat != LEPTON_FORMAT_RAW14)) {
                ESP_LOGW(TAG, "Invalid Lepton frame! Cannot get pixel temperature!");
                xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
                continue;
            }

            /* Convert the screen position to the Lepton frame coordinates */
            x = (_LeptonTask_State.ScreenPosition.x * _LeptonTask_State.RawFrame.Width) / _LeptonTask_State.ScreenPosition.Width;
            y = (_LeptonTask_State.ScreenPosition.y * _LeptonTask_State.RawFrame.Height) / _LeptonTask_State.ScreenPosition.Height;

            ESP_LOGD(TAG, "Crosshair center in Lepton Frame: (%d,%d), size (%d,%d)", x, y, _LeptonTask_State.RawFrame.Width,
                     _LeptonTask_State.RawFrame.Height);

            if (_LeptonTask_State.RawFrame.Image_Buffer != NULL) {
                if (Lepton_GetPixelTemperature(&_LeptonTask_State.Lepton,
                                               _LeptonTask_State.RawFrame.Image_Buffer[(y * _LeptonTask_State.RawFrame.Width) + x],
                                               &Temperature) == LEPTON_ERR_OK) {
                    esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_PIXEL_TEMPERATURE, &Temperature, sizeof(float), 0);
                } else {
                    ESP_LOGW(TAG, "Failed to get pixel temperature!");
                }
            } else {
                ESP_LOGW(TAG, "Image buffer is NULL, cannot get pixel temperature");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
        } else if (EventBits & LEPTON_TASK_UPDATE_SPOTMETER) {
            Lepton_Spotmeter_t Spotmeter;

            ESP_LOGD(TAG, "Getting spotmeter - I2C Bus: %p, I2C Dev: %p",
                     _LeptonTask_State.Lepton.Internal.CCI.I2C_Bus_Handle,
                     _LeptonTask_State.Lepton.Internal.CCI.I2C_Dev_Handle);

            if (Lepton_GetSpotmeter(&_LeptonTask_State.Lepton, &Spotmeter) == LEPTON_ERR_OK) {
                App_Lepton_ROI_Result_t App_Lepton_Spotmeter;

                ESP_LOGD(TAG, "Spotmeter: Spot=%uK, Min=%uK, Max=%uK",
                         Spotmeter.Value,
                         Spotmeter.Min,
                         Spotmeter.Max);

                App_Lepton_Spotmeter.Min = Spotmeter.Min - 273.0f;
                App_Lepton_Spotmeter.Max = Spotmeter.Max - 273.0f;
                App_Lepton_Spotmeter.Average = Spotmeter.Value - 273.0f;

                esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_SPOTMETER, &App_Lepton_Spotmeter, sizeof(App_Lepton_ROI_Result_t),
                               0);
            } else {
                ESP_LOGW(TAG, "Failed to read spotmeter!");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SPOTMETER);
        } else if (EventBits & LEPTON_TASK_UPDATE_SCENE_STATISTICS) {
            Lepton_SceneStatistics_t SceneStats;

            if (Lepton_GetSceneStatistics(&_LeptonTask_State.Lepton, &SceneStats) == LEPTON_ERR_OK) {
                App_Lepton_ROI_Result_t App_Lepton_Scene;

                App_Lepton_Scene.Min = SceneStats.MinIntensity;
                App_Lepton_Scene.Max = SceneStats.MaxIntensity;
                App_Lepton_Scene.Average = SceneStats.MeanIntensity;

                ESP_LOGD(TAG, "Scene Statistics: Min=%.2f°C, Max=%.2f°C, Average=%.2f°C",
                         App_Lepton_Scene.Min, App_Lepton_Scene.Max, App_Lepton_Scene.Average);

                esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_SCENE_STATISTICS, &App_Lepton_Scene,
                               sizeof(App_Lepton_ROI_Result_t), 0);
            } else {
                ESP_LOGW(TAG, "Failed to read scene statistics!");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);
        } else if (EventBits & LEPTON_TASK_UPDATE_EMISSIVITY) {
            Lepton_Error_t Error;

            Error = Lepton_SetEmissivity(&_LeptonTask_State.Lepton, static_cast<Lepton_Emissivity_t>(_LeptonTask_State.Emissivity));
            if (Error == LEPTON_ERR_OK) {
                ESP_LOGD(TAG, "Updated emissivity to %u", _LeptonTask_State.Emissivity);
            } else {
                ESP_LOGE(TAG, "Failed to update emissivity to %u!", _LeptonTask_State.Emissivity);
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);
        }
    }

    ESP_LOGD(TAG, "Lepton task shutting down");
    Lepton_Deinit(&_LeptonTask_State.Lepton);

    _LeptonTask_State.Running = false;
    _LeptonTask_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Lepton_Task_Init(void)
{
    if (_LeptonTask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Lepton Task");
    _LeptonTask_State.CurrentReadBuffer = 0;

    /* Create event group */
    _LeptonTask_State.EventGroup = xEventGroupCreate();
    if (_LeptonTask_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        return ESP_ERR_NO_MEM;
    }

    /* Create mutex for buffer synchronization */
    _LeptonTask_State.BufferMutex = xSemaphoreCreateMutex();
    if (_LeptonTask_State.BufferMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex!");
        vEventGroupDelete(_LeptonTask_State.EventGroup);
        return ESP_ERR_NO_MEM;
    }

    _LeptonTask_State.LeptonConf = LEPTON_DEFAULT_CONF;
    LEPTON_ASSIGN_FUNC(_LeptonTask_State.LeptonConf, NULL, NULL, I2CM_Write, I2CM_Read);
    LEPTON_ASSIGN_I2C_HANDLE(_LeptonTask_State.LeptonConf, DevicesManager_GetI2CBusHandle());

    /* Allocate RGB buffers - both RAW14 and RGB888 use 160x120 resolution
     * RAW14: 160x120x3 = 57,600 bytes (after conversion to RGB)
     * RGB888: 160x120x3 = 57,600 bytes (native RGB data)
     */
    size_t RGB_Buffer_Size = 160 * 120 * 3;

    _LeptonTask_State.RGB_Buffer[0] = reinterpret_cast<uint8_t *>(heap_caps_malloc(RGB_Buffer_Size, MALLOC_CAP_SPIRAM));
    _LeptonTask_State.RGB_Buffer[1] = reinterpret_cast<uint8_t *>(heap_caps_malloc(RGB_Buffer_Size, MALLOC_CAP_SPIRAM));

    if ((_LeptonTask_State.RGB_Buffer[0] == NULL) || (_LeptonTask_State.RGB_Buffer[1] == NULL)) {
        ESP_LOGE(TAG, "Can not allocate RGB buffers!");

        if (_LeptonTask_State.RGB_Buffer[0]) {
            heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        }

        if (_LeptonTask_State.RGB_Buffer[1]) {
            heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        }

        Lepton_Deinit(&_LeptonTask_State.Lepton);
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        vEventGroupDelete(_LeptonTask_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "RGB buffers allocated: 2 x %u bytes", static_cast<unsigned int>(RGB_Buffer_Size));

    /* Create internal queue to receive raw frames from VoSPI capture task */
    _LeptonTask_State.RawFrameQueue = xQueueCreate(1, sizeof(Lepton_FrameBuffer_t));
    if (_LeptonTask_State.RawFrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create raw frame queue!");

        heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        Lepton_Deinit(&_LeptonTask_State.Lepton);
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        vEventGroupDelete(_LeptonTask_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    /* Use the event loop to receive control signals from other tasks */
    esp_event_handler_register(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler, NULL);

    ESP_LOGD(TAG, "Lepton Task initialized");

    _LeptonTask_State.isInitialized = true;

    return ESP_OK;
}

void Lepton_Task_Deinit(void)
{
    if (_LeptonTask_State.isInitialized == false) {
        return;
    }

    if (_LeptonTask_State.Running) {
        Lepton_Task_Stop();
    }

    ESP_LOGI(TAG, "Deinitializing Lepton Task");

    if (_LeptonTask_State.EventGroup != NULL) {
        vEventGroupDelete(_LeptonTask_State.EventGroup);
        _LeptonTask_State.EventGroup = NULL;
    }

    esp_event_handler_unregister(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler);

    Lepton_Deinit(&_LeptonTask_State.Lepton);

    if (_LeptonTask_State.BufferMutex != NULL) {
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        _LeptonTask_State.BufferMutex = NULL;
    }

    if (_LeptonTask_State.RGB_Buffer[0] != NULL) {
        heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        _LeptonTask_State.RGB_Buffer[0] = NULL;
    }

    if (_LeptonTask_State.RGB_Buffer[1] != NULL) {
        heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        _LeptonTask_State.RGB_Buffer[1] = NULL;
    }

    if (_LeptonTask_State.RawFrameQueue != NULL) {
        vQueueDelete(_LeptonTask_State.RawFrameQueue);
        _LeptonTask_State.RawFrameQueue = NULL;
    }

    _LeptonTask_State.isInitialized = false;
}

esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Ret;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_LeptonTask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_LeptonTask_State.Running) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Starting Lepton Task");
    Ret = xTaskCreatePinnedToCore(
              Task_Lepton,
              "Task_Lepton",
              CONFIG_LEPTON_TASK_STACKSIZE,
              p_AppContext,
              CONFIG_LEPTON_TASK_PRIO,
              &_LeptonTask_State.TaskHandle,
              CONFIG_LEPTON_TASK_CORE
          );

    if (Ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Lepton Task: %d!", Ret);
        return ESP_ERR_NO_MEM;
    }

    _LeptonTask_State.Running = true;

    return ESP_OK;
}

esp_err_t Lepton_Task_Stop(void)
{
    if (_LeptonTask_State.Running == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Lepton Task");

    /* Signal task to stop */
    xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

    /* Wait for task to set Running = false before deleting itself */
    for (int i = 0; i < 20 && _LeptonTask_State.Running; i++) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    _LeptonTask_State.TaskHandle = NULL;
    _LeptonTask_State.Running = false;

    return ESP_OK;
}

bool Lepton_Task_isRunning(void)
{
    return _LeptonTask_State.Running;
}