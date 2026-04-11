/*
 * leptonTask.c
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Lepton camera task definition.
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

#include <sdkconfig.h>

#include "lepton.h"
#include "leptonTask.h"
#include "Application/application.h"
#include "Application/Manager/Devices/devicesManager.h"
#include "Application/Manager/USB/usbManager.h"
#include "Application/Manager/USB/UVC/usbUVC.h"
#include <esp_jpeg_enc.h>

#define LEPTON_TASK_STOP_REQUEST                BIT0
#define LEPTON_TASK_UPDATE_ROI_REQUEST          BIT1
#define LEPTON_TASK_UPDATE_TEMP_REQUEST         BIT2
#define LEPTON_TASK_UPDATE_UPTIME_REQUEST       BIT3
#define LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE    BIT4
#define LEPTON_TASK_UPDATE_SCENE_STATISTICS     BIT6
#define LEPTON_TASK_UPDATE_EMISSIVITY           BIT7

ESP_EVENT_DEFINE_BASE(LEPTON_TASK_EVENTS);

typedef struct {
    bool isInitialized;
    bool isRunning;
    bool ApplicationStarted;
    bool isUVCStreaming;                            /**< UVC streaming is active. */
    uint8_t *p_JpegBuffer;                          /**< JPEG compression output buffer. */
    size_t JpegBufferSize;                          /**< JPEG buffer allocated size. */
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint8_t *RGB_Buffer[2];
    uint8_t CurrentReadBuffer;
    SemaphoreHandle_t BufferMutex;
    QueueHandle_t RawFrameQueue;
    Lepton_FrameBuffer_t RawFrame;
    Lepton_Conf_t LeptonConf;
    Lepton_t Lepton;
    Settings_ROI_t ROI;
    App_GUI_Screenposition_t ScreenPosition;
    SettingsManager_ChangeNotification_t NewSetting;
} Lepton_Task_State_t;

static Lepton_Task_State_t _Lepton_Task_State;

static const char *TAG = "Lepton-Task";

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
        case SETTINGS_EVENT_LEPTON_CHANGED: {
            memcpy(&_Lepton_Task_State.NewSetting, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGD(TAG, "Lepton settings changed: ID=%d", _Lepton_Task_State.NewSetting.ID);
            ESP_LOGD(TAG, "Lepton settings changed: Value=%d", _Lepton_Task_State.NewSetting.Value);

            if (_Lepton_Task_State.NewSetting.ID == SETTINGS_ID_LEPTON_EMISSIVITY) {
                xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);
            }

            break;
        }
    }
}

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
            ESP_LOGD(TAG, "Application started event received");

            _Lepton_Task_State.ApplicationStarted = true;

            break;
        }
        case GUI_TASK_EVENT_REQUEST_ROI: {
            memcpy(&_Lepton_Task_State.ROI, p_Data, sizeof(Settings_ROI_t));

            xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_FPA_AUX_TEMP: {
            xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
            break;
        }
        case GUI_TASK_EVENT_REQUEST_UPTIME: {
            xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_PIXEL_TEMPERATURE: {
            _Lepton_Task_State.ScreenPosition = *static_cast<const App_GUI_Screenposition_t *>(p_Data);

            xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_SCENE_STATISTICS: {
            xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled GUI event ID: 0x%X", ID);

            break;
        }
    }
}

/** @brief                  Event handler for the USB Manager to receive UVC streaming events.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_USB_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "USB event received: ID=%d", ID);

    switch (ID) {
        case USB_EVENT_UVC_STREAMING_START: {
            ESP_LOGD(TAG, "UVC streaming started - redirecting frames to USB");
            _Lepton_Task_State.isUVCStreaming = true;

            break;
        }
        case USB_EVENT_UVC_STREAMING_STOP: {
            ESP_LOGD(TAG, "UVC streaming stopped - resuming GUI frames");
            _Lepton_Task_State.isUVCStreaming = false;

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            if (_Lepton_Task_State.isUVCStreaming) {
                ESP_LOGI(TAG, "USB deinitialized while UVC streaming - resuming GUI frames");
                _Lepton_Task_State.isUVCStreaming = false;
            }

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief Loads the Lepton settings from the Settings Manager and applies them to the Lepton.
 */
static void Lepton_LoadSettings(void)
{
    Settings_Lepton_t LeptonSettings;

    SettingsManager_GetLepton(&LeptonSettings);

    ESP_LOGI(TAG, "Loading Lepton settings...");
    ESP_LOGI(TAG, "Emissivity: 0x%X", LeptonSettings.CurrentEmissivity);

    Lepton_SetEmissivity(&_Lepton_Task_State.Lepton, static_cast<Lepton_Emissivity_t>(LeptonSettings.CurrentEmissivity));
}

/** @brief Resets the Lepton camera.
 */
static void Lepton_Reset(bool Enable)
{
    //DevicesManager_LeptonReset(Enable);
}

/** @brief Powers down the Lepton camera.
 */
static void Lepton_PowerDown(bool Enable)
{
    /* PowerDown true  = disable Lepton power supply
     * PowerDown false = enable Lepton power supply */
    //DevicesManager_SetLeptonPower(!Enable);
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
    App_Context = static_cast<App_Context_t *>(p_Parameters);

    ESP_LOGD(TAG, "Lepton task started on core %d", xPortGetCoreID());

    Lepton_Error = Lepton_Init(&_Lepton_Task_State.Lepton, &_Lepton_Task_State.LeptonConf);
    if (Lepton_Error != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Lepton initialization failed with error: 0x%X!", Lepton_Error);

        esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_ERROR, NULL, 0, pdMS_TO_TICKS(500));
        esp_task_wdt_delete(NULL);

        vTaskDelete(NULL);
    }

    /* Format serial number as readable string: XXXX-XXXX-XXXX-XXXX */
    snprintf(DeviceInfo.SerialNumber, sizeof(DeviceInfo.SerialNumber),
             "%02X%02X-%02X%02X-%02X%02X-%02X%02X",
             _Lepton_Task_State.Lepton.SerialNumber[0], _Lepton_Task_State.Lepton.SerialNumber[1],
             _Lepton_Task_State.Lepton.SerialNumber[2], _Lepton_Task_State.Lepton.SerialNumber[3],
             _Lepton_Task_State.Lepton.SerialNumber[4], _Lepton_Task_State.Lepton.SerialNumber[5],
             _Lepton_Task_State.Lepton.SerialNumber[6], _Lepton_Task_State.Lepton.SerialNumber[7]);
    memcpy(DeviceInfo.PartNumber, _Lepton_Task_State.Lepton.PartNumber, sizeof(DeviceInfo.PartNumber));

    snprintf(DeviceInfo.SoftwareRevision.GPP_Revision, sizeof(DeviceInfo.SoftwareRevision.GPP_Revision),
             "%u.%u.%u",
             _Lepton_Task_State.Lepton.SoftwareVersion.gpp_major,
             _Lepton_Task_State.Lepton.SoftwareVersion.gpp_minor,
             _Lepton_Task_State.Lepton.SoftwareVersion.gpp_build);

    snprintf(DeviceInfo.SoftwareRevision.DSP_Revision, sizeof(DeviceInfo.SoftwareRevision.DSP_Revision),
             "%u.%u.%u",
             _Lepton_Task_State.Lepton.SoftwareVersion.dsp_major,
             _Lepton_Task_State.Lepton.SoftwareVersion.dsp_minor,
             _Lepton_Task_State.Lepton.SoftwareVersion.dsp_build);

    ESP_LOGI(TAG, "	Part number: %s", DeviceInfo.PartNumber);
    ESP_LOGI(TAG, "	Serial number: %s", DeviceInfo.SerialNumber);
    ESP_LOGI(TAG, "	GPP revision: %s", DeviceInfo.SoftwareRevision.GPP_Revision);
    ESP_LOGI(TAG, "	DSP revision: %s", DeviceInfo.SoftwareRevision.DSP_Revision);

    esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_READY, &DeviceInfo, sizeof(App_Lepton_Device_t), pdMS_TO_TICKS(500));

    while (_Lepton_Task_State.ApplicationStarted == false) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    Lepton_FluxLinearParams_t FluxParams;
    Lepton_GetFluxLinearParameters(&_Lepton_Task_State.Lepton, &FluxParams);
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

    Lepton_LoadSettings();

    if (Lepton_StartCapture(&_Lepton_Task_State.Lepton, _Lepton_Task_State.RawFrameQueue) != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Can not start image capturing!");

        esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_ERROR, NULL, 0, pdMS_TO_TICKS(500));

        /* Critical error - cannot continue without capture task */
        _Lepton_Task_State.isRunning = false;
        _Lepton_Task_State.TaskHandle = NULL;

        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);

        return;
    }

    while (_Lepton_Task_State.isRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Wait for a new raw frame with longer timeout to avoid busy waiting */
        if (xQueueReceive(_Lepton_Task_State.RawFrameQueue, &_Lepton_Task_State.RawFrame, pdMS_TO_TICKS(500)) == pdTRUE) {
            uint8_t WriteBufferIdx;
            uint8_t *WriteBuffer;
            int16_t Min = 0;
            int16_t Max = 0;
            Lepton_Telemetry_t Telemetry;
            Lepton_VideoFormat_t VideoFormat;

            if (_Lepton_Task_State.RawFrame.Telemetry_Buffer != NULL) {
                memcpy(&Telemetry, _Lepton_Task_State.RawFrame.Telemetry_Buffer, sizeof(Lepton_Telemetry_t));
                ESP_LOGD(TAG, "Telemetry - FrameCounter: %u, FPA_Temp: %uK, Housing_Temp: %uK",
                         Telemetry.FrameCounter,
                         Telemetry.FPA_Temp,
                         Telemetry.Housing_Temp);
            }

            ESP_LOGD(TAG, "Processing frame...");

            /* Determine which buffer to write to (ping-pong) */
            if (xSemaphoreTake(_Lepton_Task_State.BufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                /* Find a buffer that's not currently being read */
                WriteBufferIdx = (_Lepton_Task_State.CurrentReadBuffer + 1) % 2;
                WriteBuffer = _Lepton_Task_State.RGB_Buffer[WriteBufferIdx];
                xSemaphoreGive(_Lepton_Task_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer selection!");

                continue;
            }

            /* Process frame based on video format */
            Lepton_GetVideoFormat(&_Lepton_Task_State.Lepton, &VideoFormat);

            if (VideoFormat == LEPTON_FORMAT_RGB888) {
                /* RGB888: Data is already in RGB format, just copy it */
                size_t ImageSize = _Lepton_Task_State.RawFrame.Width * _Lepton_Task_State.RawFrame.Height *
                                   _Lepton_Task_State.RawFrame.BytesPerPixel;

                memcpy(WriteBuffer, _Lepton_Task_State.RawFrame.Image_Buffer, ImageSize);

                ESP_LOGD(TAG, "Copied RGB888 frame: %ux%u (%u bytes)", _Lepton_Task_State.RawFrame.Width,
                         _Lepton_Task_State.RawFrame.Height, static_cast<unsigned int>(ImageSize));
            } else {
                /* RAW14: Convert to RGB */
                Lepton_Raw14ToRGB(&_Lepton_Task_State.Lepton, _Lepton_Task_State.RawFrame.Image_Buffer, WriteBuffer, &Min, &Max,
                                  _Lepton_Task_State.RawFrame.Width,
                                  _Lepton_Task_State.RawFrame.Height);
            }

            /* Mark buffer as ready and update read buffer index */
            if (xSemaphoreTake(_Lepton_Task_State.BufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                _Lepton_Task_State.CurrentReadBuffer = WriteBufferIdx;
                xSemaphoreGive(_Lepton_Task_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer ready!");

                continue;
            }

            /* If UVC streaming is active, also send frames to USB */
            if (_Lepton_Task_State.isUVCStreaming && (_Lepton_Task_State.p_JpegBuffer != NULL)) {
                /* Double-check streaming state to avoid submitting after host closed stream */
                if (USBUVC_IsStreaming() == false) {
                    _Lepton_Task_State.isUVCStreaming = false;

                    ESP_LOGI(TAG, "UVC streaming ended - resuming GUI frames");
                } else {
                    jpeg_error_t JpegError;
                    jpeg_enc_config_t EncConfig = DEFAULT_JPEG_ENC_CONFIG();
                    jpeg_enc_handle_t JpegEncoder = NULL;
                    int JpegSize = 0;

                    /* Configure JPEG encoder for Lepton thermal camera */
                    EncConfig.src_type = JPEG_PIXEL_FORMAT_RGB888;
                    EncConfig.subsampling = JPEG_SUBSAMPLE_420;
                    EncConfig.quality = 80;
                    EncConfig.width = _Lepton_Task_State.RawFrame.Width;
                    EncConfig.height = _Lepton_Task_State.RawFrame.Height;

                    /* Create encoder instance */
                    JpegError = jpeg_enc_open(&EncConfig, &JpegEncoder);
                    if (JpegError == JPEG_ERR_OK) {
                        JpegError = jpeg_enc_process(JpegEncoder, WriteBuffer,
                                                     static_cast<int>(_Lepton_Task_State.RawFrame.Width * _Lepton_Task_State.RawFrame.Height * 3),
                                                     _Lepton_Task_State.p_JpegBuffer,
                                                     static_cast<int>(_Lepton_Task_State.JpegBufferSize),
                                                     &JpegSize);
                        if (JpegError == JPEG_ERR_OK) {
                            /* Submit JPEG frame to UVC */
                            esp_err_t UVCError = USBUVC_SubmitFrame(_Lepton_Task_State.p_JpegBuffer, static_cast<size_t>(JpegSize));
                            if (UVCError == ESP_ERR_INVALID_STATE) {
                                /* Streaming was stopped - immediately stop submitting.
                                   Next frame iteration will route to GUI path. */
                                _Lepton_Task_State.isUVCStreaming = false;

                                ESP_LOGI(TAG, "UVC stream no longer active - resuming GUI frames");
                            } else if (UVCError != ESP_OK) {
                                ESP_LOGW(TAG, "Failed to submit frame to UVC: 0x%X", UVCError);
                            } else {
                                ESP_LOGD(TAG, "UVC frame submitted: %d bytes JPEG", JpegSize);
                            }
                        } else {
                            ESP_LOGW(TAG, "JPEG encoding failed: 0x%X", JpegError);
                        }

                        jpeg_enc_close(JpegEncoder);
                    } else {
                        ESP_LOGW(TAG, "Failed to open JPEG encoder: 0x%X", JpegError);
                    }
                }
            }

            /* Send frame notification to GUI/network task */
            App_Lepton_FrameReady_t FrameEvent = {
                .Buffer = WriteBuffer,
                .Width = _Lepton_Task_State.RawFrame.Width,
                .Height = _Lepton_Task_State.RawFrame.Height,
                .Channels = 3,
                .Min = Min,
                .Max = Max
            };

            /* Use xQueueOverwrite to always have the latest frame */
            xQueueOverwrite(App_Context->Lepton_FrameEventQueue, &FrameEvent);
            ESP_LOGD(TAG, "Frame sent to queue successfully");
        } else {
            ESP_LOGW(TAG, "No raw frame received from VoSPI");
        }

        EventBits = xEventGroupGetBits(_Lepton_Task_State.EventGroup);
        if (EventBits & LEPTON_TASK_UPDATE_ROI_REQUEST) {
            Lepton_ROI_t ROI;
            Lepton_Error_t Error;

            ROI.Start_Col = _Lepton_Task_State.ROI.x;
            ROI.Start_Row = _Lepton_Task_State.ROI.y;
            ROI.End_Col = _Lepton_Task_State.ROI.x + _Lepton_Task_State.ROI.w - 1;
            ROI.End_Row = _Lepton_Task_State.ROI.y + _Lepton_Task_State.ROI.h - 1;

            if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                switch (_Lepton_Task_State.ROI.Type) {
                    case ROI_TYPE_SPOTMETER: {
                        Error = Lepton_SetSpotmeterROI(&_Lepton_Task_State.Lepton, &ROI);

                        break;
                    }
                    case ROI_TYPE_SCENE: {
                        Error = Lepton_SetSceneROI(&_Lepton_Task_State.Lepton, &ROI);

                        break;
                    }
                    case ROI_TYPE_AGC: {
                        Error = Lepton_SetAGCROI(&_Lepton_Task_State.Lepton, &ROI);

                        break;
                    }
                    case ROI_TYPE_VIDEO_FOCUS: {
                        Error = Lepton_SetVideoFocusROI(&_Lepton_Task_State.Lepton, &ROI);

                        break;
                    }
                    default: {
                        ESP_LOGW(TAG, "Invalid ROI type in GUI event: 0x%X", _Lepton_Task_State.ROI.Type);
                        DevicesManager_ReleaseI2CBus();

                        return;
                    }
                }

                DevicesManager_ReleaseI2CBus();

                if (Error == LEPTON_ERR_OK) {
                    ESP_LOGD(TAG, "New Lepton ROI (Type %d) - Start_Col: %u, Start_Row: %u, End_Col: %u, End_Row: %u",
                             _Lepton_Task_State.ROI.Type,
                             ROI.Start_Col,
                             ROI.Start_Row,
                             ROI.End_Col,
                             ROI.End_Row);
                } else {
                    ESP_LOGE(TAG, "Failed to update Lepton ROI with type %d!", _Lepton_Task_State.ROI.Type);
                }
            } else {
                ESP_LOGW(TAG, "I2C bus busy, skipping ROI update!");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);
        }

        if (EventBits & LEPTON_TASK_UPDATE_TEMP_REQUEST) {
            uint16_t FPA_Temp;
            uint16_t AUX_Temp;
            App_Lepton_Temperatures_t Temperatures;

            if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                Lepton_GetTemperature(&_Lepton_Task_State.Lepton, &FPA_Temp, &AUX_Temp);
                DevicesManager_ReleaseI2CBus();

                Temperatures.FPA = (static_cast<float>(FPA_Temp) * 0.01f) - 273.0f;
                Temperatures.AUX = (static_cast<float>(AUX_Temp) * 0.01f) - 273.0f;
                esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_FPA_AUX_TEMP, &Temperatures, sizeof(App_Lepton_Temperatures_t), 0);
            } else {
                ESP_LOGW(TAG, "I2C bus busy, skipping temperature read!");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
        }

        if (EventBits & LEPTON_TASK_UPDATE_UPTIME_REQUEST) {
            uint32_t Uptime;

            if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                Uptime = Lepton_GetUptime(&_Lepton_Task_State.Lepton);
                DevicesManager_ReleaseI2CBus();

                esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_UPTIME, &Uptime, sizeof(uint32_t), 0);
            } else {
                ESP_LOGW(TAG, "I2C bus busy, skipping uptime read!");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);
        }

        if (EventBits & LEPTON_TASK_STOP_REQUEST) {
            ESP_LOGI(TAG, "Stop request received");

            _Lepton_Task_State.isRunning = false;

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE) {
            int16_t x;
            int16_t y;
            float Temperature;
            Lepton_VideoFormat_t VideoFormat;

            Lepton_GetVideoFormat(&_Lepton_Task_State.Lepton, &VideoFormat);
            if (((_Lepton_Task_State.RawFrame.Width == 0) || (_Lepton_Task_State.RawFrame.Height == 0)) &&
                (VideoFormat != LEPTON_FORMAT_RAW14)) {
                ESP_LOGW(TAG, "Invalid Lepton frame! Cannot get pixel temperature!");

                xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

                continue;
            }

            /* Convert the screen position to the Lepton frame coordinates */
            x = (_Lepton_Task_State.ScreenPosition.x * _Lepton_Task_State.RawFrame.Width) / _Lepton_Task_State.ScreenPosition.Width;
            y = (_Lepton_Task_State.ScreenPosition.y * _Lepton_Task_State.RawFrame.Height) /
                _Lepton_Task_State.ScreenPosition.Height;

            ESP_LOGD(TAG, "Crosshair center in Lepton Frame: (%d,%d), size (%d,%d)", x, y, _Lepton_Task_State.RawFrame.Width,
                     _Lepton_Task_State.RawFrame.Height);

            if (_Lepton_Task_State.RawFrame.Image_Buffer != NULL) {
                if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                    Lepton_Error_t LeptonError = Lepton_GetPixelTemperature(&_Lepton_Task_State.Lepton,
                                                                            _Lepton_Task_State.RawFrame.Image_Buffer[(y * _Lepton_Task_State.RawFrame.Width) + x],
                                                                            &Temperature);
                    DevicesManager_ReleaseI2CBus();

                    if (LeptonError == LEPTON_ERR_OK) {
                        esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_PIXEL_TEMPERATURE, &Temperature, sizeof(float), 0);
                    } else {
                        ESP_LOGW(TAG, "Failed to get pixel temperature: 0x%X", LeptonError);
                    }
                } else {
                    ESP_LOGW(TAG, "I2C bus busy, skipping pixel temperature read!");
                }
            } else {
                ESP_LOGW(TAG, "Image buffer is NULL, cannot get pixel temperature");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
        }

        if (EventBits & LEPTON_TASK_UPDATE_SCENE_STATISTICS) {
            Lepton_SceneStatistics_t SceneStats;

            if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                if (Lepton_GetSceneStatistics(&_Lepton_Task_State.Lepton, &SceneStats) == LEPTON_ERR_OK) {
                    App_Lepton_ROI_Result_t App_Lepton_Scene;

                    App_Lepton_Scene.Min = SceneStats.MinIntensity;
                    App_Lepton_Scene.Max = SceneStats.MaxIntensity;
                    App_Lepton_Scene.Average = SceneStats.MeanIntensity;

                    ESP_LOGD(TAG, "Scene Statistics: Min=%.2f°C, Max=%.2f°C, Average=%.2f°C",
                             App_Lepton_Scene.Min, App_Lepton_Scene.Max, App_Lepton_Scene.Average);

                    DevicesManager_ReleaseI2CBus();

                    esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_SCENE_STATISTICS, &App_Lepton_Scene,
                                   sizeof(App_Lepton_ROI_Result_t), 0);
                } else {
                    DevicesManager_ReleaseI2CBus();

                    ESP_LOGW(TAG, "Failed to read scene statistics!");
                }
            } else {
                ESP_LOGW(TAG, "I2C bus busy, skipping scene statistics read!");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);
        }

        if (EventBits & LEPTON_TASK_UPDATE_EMISSIVITY) {
            Lepton_Error_t Error;

            if (DevicesManager_AcquireI2CBus(portMAX_DELAY) == ESP_OK) {
                Error = Lepton_SetEmissivity(&_Lepton_Task_State.Lepton,
                                             static_cast<Lepton_Emissivity_t>(_Lepton_Task_State.NewSetting.Value));
                DevicesManager_ReleaseI2CBus();

                if (Error == LEPTON_ERR_OK) {
                    ESP_LOGD(TAG, "Updated emissivity to %u", _Lepton_Task_State.NewSetting.Value);
                } else {
                    ESP_LOGE(TAG, "Failed to update emissivity to %u!", _Lepton_Task_State.NewSetting.Value);
                }
            } else {
                ESP_LOGW(TAG, "I2C bus busy, skipping emissivity update!");
            }

            xEventGroupClearBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);
        }
    }

    ESP_LOGD(TAG, "Lepton task shutting down");
    Lepton_Deinit(&_Lepton_Task_State.Lepton);

    _Lepton_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Lepton_Task_Init(void)
{
    uint32_t Caps;

    if (_Lepton_Task_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Lepton Task");
    _Lepton_Task_State.CurrentReadBuffer = 0;

    _Lepton_Task_State.EventGroup = xEventGroupCreate();
    if (_Lepton_Task_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    _Lepton_Task_State.BufferMutex = xSemaphoreCreateMutex();
    if (_Lepton_Task_State.BufferMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex!");

        vEventGroupDelete(_Lepton_Task_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    _Lepton_Task_State.LeptonConf = LEPTON_DEFAULT_CONF;
    LEPTON_ASSIGN_FUNC(_Lepton_Task_State.LeptonConf, NULL, NULL, I2CM_Write, I2CM_Read);
    _Lepton_Task_State.LeptonConf.CCI.I2C_WriteRead = I2CM_WriteRead;
    LEPTON_ASSIGN_I2C_HANDLE(_Lepton_Task_State.LeptonConf, DevicesManager_GetI2CBusHandle());
    _Lepton_Task_State.LeptonConf.Reset = Lepton_Reset;
    _Lepton_Task_State.LeptonConf.PowerDown = Lepton_PowerDown;

    /* Allocate RGB buffers - both RAW14 and RGB888 use 160x120 resolution
     * RAW14: 160x120x3 = 57,600 bytes (after conversion to RGB)
     * RGB888: 160x120x3 = 57,600 bytes (native RGB data)
     */
    size_t RGB_Buffer_Size = 160 * 120 * 3;

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SIMD | MALLOC_CAP_SPIRAM;
#else
    Caps = MALLOC_CAP_SIMD;
#endif

    _Lepton_Task_State.RGB_Buffer[0] = static_cast<uint8_t *>(heap_caps_malloc(RGB_Buffer_Size, Caps));
    _Lepton_Task_State.RGB_Buffer[1] = static_cast<uint8_t *>(heap_caps_malloc(RGB_Buffer_Size, Caps));

    if ((_Lepton_Task_State.RGB_Buffer[0] == NULL) || (_Lepton_Task_State.RGB_Buffer[1] == NULL)) {
        ESP_LOGE(TAG, "Can not allocate RGB buffers!");

        if (_Lepton_Task_State.RGB_Buffer[0]) {
            heap_caps_free(_Lepton_Task_State.RGB_Buffer[0]);
        }

        if (_Lepton_Task_State.RGB_Buffer[1]) {
            heap_caps_free(_Lepton_Task_State.RGB_Buffer[1]);
        }

        Lepton_Deinit(&_Lepton_Task_State.Lepton);
        vSemaphoreDelete(_Lepton_Task_State.BufferMutex);
        vEventGroupDelete(_Lepton_Task_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "RGB buffers allocated: 2 x %u bytes", static_cast<unsigned int>(RGB_Buffer_Size));

    /* Create internal queue to receive raw frames from VoSPI capture task */
    _Lepton_Task_State.RawFrameQueue = xQueueCreate(1, sizeof(Lepton_FrameBuffer_t));
    if (_Lepton_Task_State.RawFrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create raw frame queue!");

        heap_caps_free(_Lepton_Task_State.RGB_Buffer[0]);
        heap_caps_free(_Lepton_Task_State.RGB_Buffer[1]);
        Lepton_Deinit(&_Lepton_Task_State.Lepton);
        vSemaphoreDelete(_Lepton_Task_State.BufferMutex);
        vEventGroupDelete(_Lepton_Task_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    esp_event_handler_register(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler, NULL);

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM;
#else
    Caps = 0;
#endif

    /* Allocate JPEG compression buffer in PSRAM */
    _Lepton_Task_State.JpegBufferSize = 160 * 120 * 3;
    _Lepton_Task_State.p_JpegBuffer = static_cast<uint8_t *>(heap_caps_malloc(_Lepton_Task_State.JpegBufferSize, Caps));
    if (_Lepton_Task_State.p_JpegBuffer == NULL) {
        ESP_LOGW(TAG, "Failed to allocate JPEG buffer - UVC streaming will not work");
    } else {
        ESP_LOGD(TAG, "JPEG buffer allocated: %u bytes", static_cast<unsigned int>(_Lepton_Task_State.JpegBufferSize));
    }

    _Lepton_Task_State.isUVCStreaming = false;

    ESP_LOGD(TAG, "Lepton Task initialized");

    _Lepton_Task_State.isInitialized = true;

    return ESP_OK;
}

void Lepton_Task_Deinit(void)
{
    if (_Lepton_Task_State.isInitialized == false) {
        return;
    }

    if (_Lepton_Task_State.isRunning) {
        Lepton_Task_Stop();
    }

    ESP_LOGI(TAG, "Deinitializing Lepton Task");

    if (_Lepton_Task_State.EventGroup != NULL) {
        vEventGroupDelete(_Lepton_Task_State.EventGroup);
        _Lepton_Task_State.EventGroup = NULL;
    }

    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler);
    esp_event_handler_unregister(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler);

    if (_Lepton_Task_State.p_JpegBuffer != NULL) {
        heap_caps_free(_Lepton_Task_State.p_JpegBuffer);
        _Lepton_Task_State.p_JpegBuffer = NULL;
    }

    Lepton_Deinit(&_Lepton_Task_State.Lepton);

    if (_Lepton_Task_State.BufferMutex != NULL) {
        vSemaphoreDelete(_Lepton_Task_State.BufferMutex);
        _Lepton_Task_State.BufferMutex = NULL;
    }

    if (_Lepton_Task_State.RGB_Buffer[0] != NULL) {
        heap_caps_free(_Lepton_Task_State.RGB_Buffer[0]);
        _Lepton_Task_State.RGB_Buffer[0] = NULL;
    }

    if (_Lepton_Task_State.RGB_Buffer[1] != NULL) {
        heap_caps_free(_Lepton_Task_State.RGB_Buffer[1]);
        _Lepton_Task_State.RGB_Buffer[1] = NULL;
    }

    if (_Lepton_Task_State.RawFrameQueue != NULL) {
        vQueueDelete(_Lepton_Task_State.RawFrameQueue);
        _Lepton_Task_State.RawFrameQueue = NULL;
    }

    _Lepton_Task_State.isInitialized = false;
}

esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Lepton_Task_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_Lepton_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already Running");

        return ESP_OK;
    }

    _Lepton_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Lepton Task");

    Error = xTaskCreatePinnedToCore(Task_Lepton, "Task_Lepton", CONFIG_LEPTON_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_LEPTON_TASK_PRIO, &_Lepton_Task_State.TaskHandle, CONFIG_LEPTON_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Lepton Task: 0x%X!", Error);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Lepton_Task_Stop(void)
{
    if (_Lepton_Task_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Lepton Task");

    xEventGroupSetBits(_Lepton_Task_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Lepton_Task_IsRunning(void)
{
    return _Lepton_Task_State.isRunning;
}