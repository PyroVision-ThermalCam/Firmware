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
#include "AppDiag/appDiag.h"
#include "Application/Manager/Devices/devicesManager.h"
#include "Application/Manager/USB/usbManager.h"
#include "Application/Manager/USB/UVC/usbUVC.h"
#include "Application/Manager/Network/Server/ImageEncoder/JPEG/jpegEncoder.h"

#define LEPTON_TASK_STOP_REQUEST                BIT0
#define LEPTON_TASK_UPDATE_ROI_REQUEST          BIT1
#define LEPTON_TASK_UPDATE_TEMP_REQUEST         BIT2
#define LEPTON_TASK_UPDATE_UPTIME_REQUEST       BIT3
#define LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE    BIT4
#define LEPTON_TASK_UPDATE_SCENE_STATISTICS     BIT6
#define LEPTON_TASK_UPDATE_EMISSIVITY           BIT7
#define LEPTON_TASK_TEMPERATURE_STATUS_CHANGED  BIT8

ESP_EVENT_DEFINE_BASE(LEPTON_TASK_EVENTS);

/** @brief Internal runtime state of the Lepton camera task.
 *         Holds all FreeRTOS primitives, double-buffered RGB output, raw frame queue,
 *         Lepton driver handles, and the latest ROI and temperature data used by the task loop.
 */
typedef struct {
    bool IsInitialized;                                 /**< true after Lepton_Task_Init() has completed successfully. */
    bool IsRunning;                                     /**< true while the FreeRTOS task is executing. */
    bool ApplicationStarted;                            /**< true once the GUI task has signalled APP_STARTED. */
    bool IsUVCStreaming;                                /**< true while a UVC host is actively receiving frames. */
    TaskHandle_t TaskHandle;                            /**< FreeRTOS task handle; NULL before Lepton_Task_Start(). */
    EventGroupHandle_t EventGroup;                      /**< Event group used for intra-task synchronisation. */
    uint8_t *RGB_Buffer[2];                             /**< Double-buffered PSRAM RGB888 output; ping-pong scheme. */
    uint8_t CurrentReadBuffer;                          /**< Index (0 or 1) of the buffer currently safe to read. */
    SemaphoreHandle_t BufferMutex;                      /**< Mutex protecting RGB_Buffer access across tasks. */
    QueueHandle_t RawFrameQueue;                        /**< Queue carrying raw Lepton frame pointers from the ISR. */
    Lepton_FrameBuffer_t RawFrame;                      /**< Scratch buffer for the latest raw Lepton frame. */
    Lepton_Conf_t LeptonConf;                           /**< Active Lepton camera configuration snapshot. */
    Lepton_t Lepton;                                    /**< Lepton driver handle used for all CCI/SPI operations. */
    Settings_ROI_t ROI;                                 /**< Current region-of-interest settings. */
    App_GUI_Screenposition_t ScreenPosition;            /**< Mapping from Lepton pixel coordinates to display pixels. */
    App_Devices_Temperature_t TemperatureInfo;          /**< Latest ambient/housing temperature readings. */
    SettingsManager_ChangeNotification_t NewSetting;    /**< Staging area for incoming settings-change notifications. */
} Lepton_Task_State_t;

static Lepton_Task_State_t _LeptonTaskState;

static const char *TAG = "Lepton-Task";

/** @brief          Per-transaction I2C write wrapper for the Lepton CCI.
 *                  Acquires and releases the shared I2C bus mutex for each individual I2C
 *                  operation. This allows CCI_WaitBusy to release the bus between its 10 ms
 *                  polling intervals, preventing other tasks (e.g. displayboard interrupt
 *                  handler) from being starved for the full 5-second CCI timeout.
 *  @param p_Dev    I2C master device handle for the Lepton CCI
 *  @param p_Data   Pointer to the data buffer to write
 *  @param Length   Number of bytes to write
 *  @return         esp_err_t result code from the I2CM_Write operation
 */
static int32_t Lepton_CCI_Write(i2c_master_dev_handle_t *p_Dev, const uint8_t *p_Data, uint32_t Length)
{
    int32_t Result;

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Result = I2CM_Write(p_Dev, p_Data, Length);
    DevicesManager_ReleaseI2CBus();

    return Result;
}

/** @brief          Per-transaction I2C read wrapper for the Lepton CCI.
 *                  See Lepton_CCI_Write() for rationale.
 *  @param p_Dev    I2C master device handle for the Lepton CCI
 *  @param p_Data   Pointer to the data buffer to read into
 *  @param Length   Number of bytes to read
 *  @return         esp_err_t result code from the I2CM_Read operation
 */
static int32_t Lepton_CCI_Read(i2c_master_dev_handle_t *p_Dev, uint8_t *p_Data, uint32_t Length)
{
    int32_t Result;

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Result = I2CM_Read(p_Dev, p_Data, Length);
    DevicesManager_ReleaseI2CBus();

    return Result;
}

/** @brief              Per-transaction I2C write-read wrapper for the Lepton CCI.
 *                      See Lepton_CCI_Write() for rationale.
 *  @param p_Dev        I2C master device handle for the Lepton CCI
 *  @param p_WriteData  Pointer to the data buffer to write (e.g. register address bytes)
 *  @param WriteLength  Number of bytes to write
 *  @param p_ReadData   Pointer to the data buffer to read into
 *  @param ReadLength   Number of bytes to read
 *  @return             esp_err_t result code from the I2CM_WriteRead operation
 */
static int32_t Lepton_CCI_WriteRead(i2c_master_dev_handle_t *p_Dev,
                                        const uint8_t *p_WriteData, uint32_t WriteLength,
                                        uint8_t *p_ReadData, uint32_t ReadLength)
{
    int32_t Result;

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Result = I2CM_WriteRead(p_Dev, p_WriteData, WriteLength, p_ReadData, ReadLength);
    DevicesManager_ReleaseI2CBus();

    return Result;
}

/** @brief                  Event handler for the Devices task events to receive updates when settings are changed.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Devices_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Devices task event received: ID=%d", ID);

    switch (ID) {
        case DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE: {
            memcpy(&_LeptonTaskState.TemperatureInfo, p_Data, sizeof(App_Devices_Temperature_t));

            ESP_LOGD(TAG, "Temperature status updated: Temperature = %.2f\xC2\xB0""C",
                     _LeptonTaskState.TemperatureInfo.TempSensor);

            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_TEMPERATURE_STATUS_CHANGED);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled devices task event ID: 0x%X", ID);

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

            _LeptonTaskState.ApplicationStarted = true;

            break;
        }
        case GUI_TASK_EVENT_REQUEST_ROI: {
            memcpy(&_LeptonTaskState.ROI, p_Data, sizeof(Settings_ROI_t));

            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_FPA_AUX_TEMP: {
            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
            break;
        }
        case GUI_TASK_EVENT_REQUEST_UPTIME: {
            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_PIXEL_TEMPERATURE: {
            _LeptonTaskState.ScreenPosition = *static_cast<const App_GUI_Screenposition_t *>(p_Data);

            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

            break;
        }
        case GUI_TASK_EVENT_REQUEST_SCENE_STATISTICS: {
            xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled GUI task event ID: 0x%X", ID);

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
        case SETTINGS_EVENT_LEPTON_CHANGED: {
            memcpy(&_LeptonTaskState.NewSetting, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            ESP_LOGD(TAG, "Lepton settings changed: ID=%d", _LeptonTaskState.NewSetting.ID);
            ESP_LOGD(TAG, "Lepton settings changed: Value=%d", _LeptonTaskState.NewSetting.Value);

            if (_LeptonTaskState.NewSetting.ID == SETTINGS_ID_LEPTON_EMISSIVITY) {
                xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);
            }

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
            _LeptonTaskState.IsUVCStreaming = true;

            break;
        }
        case USB_EVENT_UVC_STREAMING_STOP: {
            ESP_LOGD(TAG, "UVC streaming stopped - resuming GUI frames");
            _LeptonTaskState.IsUVCStreaming = false;

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            if (_LeptonTaskState.IsUVCStreaming) {
                ESP_LOGI(TAG, "USB deinitialized while UVC streaming - resuming GUI frames");
                _LeptonTaskState.IsUVCStreaming = false;
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

    ESP_LOGD(TAG, "Loading Lepton settings...");
    ESP_LOGD(TAG, "Emissivity: 0x%X", LeptonSettings.CurrentEmissivity);

    Lepton_SetEmissivity(&_LeptonTaskState.Lepton, static_cast<Lepton_Emissivity_t>(LeptonSettings.CurrentEmissivity));
}

/** @brief          Resets the Lepton camera.
 *  @param Enable   true to reset (RESET_L LOW), false to release (RESET_L HIGH)
 */
static void Lepton_Reset(bool Enable)
{
    //DevicesManager_LeptonReset(Enable);
}

/** @brief          Powers down the Lepton camera.
 *  @param Enable   true to power down, false to power up
 */
static void Lepton_PowerDown(bool Enable)
{
    DevicesManager_SetLeptonPower(Enable == false);
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

    Lepton_Error = Lepton_Init(&_LeptonTaskState.Lepton, &_LeptonTaskState.LeptonConf);
    if (Lepton_Error != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Lepton initialization failed with error: 0x%X!", Lepton_Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, static_cast<esp_err_t>(Lepton_Error));

        esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_ERROR, NULL, 0, pdMS_TO_TICKS(500));
        esp_task_wdt_delete(NULL);

        vTaskDelete(NULL);
    }

    /* Format serial number as readable string: XXXX-XXXX-XXXX-XXXX */
    snprintf(DeviceInfo.SerialNumber, sizeof(DeviceInfo.SerialNumber),
             "%02X%02X-%02X%02X-%02X%02X-%02X%02X",
             _LeptonTaskState.Lepton.SerialNumber[0], _LeptonTaskState.Lepton.SerialNumber[1],
             _LeptonTaskState.Lepton.SerialNumber[2], _LeptonTaskState.Lepton.SerialNumber[3],
             _LeptonTaskState.Lepton.SerialNumber[4], _LeptonTaskState.Lepton.SerialNumber[5],
             _LeptonTaskState.Lepton.SerialNumber[6], _LeptonTaskState.Lepton.SerialNumber[7]);
    memcpy(DeviceInfo.PartNumber, _LeptonTaskState.Lepton.PartNumber, sizeof(DeviceInfo.PartNumber));

    snprintf(DeviceInfo.SoftwareRevision.GPP_Revision, sizeof(DeviceInfo.SoftwareRevision.GPP_Revision),
             "%u.%u.%u",
             _LeptonTaskState.Lepton.SoftwareVersion.gpp_major,
             _LeptonTaskState.Lepton.SoftwareVersion.gpp_minor,
             _LeptonTaskState.Lepton.SoftwareVersion.gpp_build);

    snprintf(DeviceInfo.SoftwareRevision.DSP_Revision, sizeof(DeviceInfo.SoftwareRevision.DSP_Revision),
             "%u.%u.%u",
             _LeptonTaskState.Lepton.SoftwareVersion.dsp_major,
             _LeptonTaskState.Lepton.SoftwareVersion.dsp_minor,
             _LeptonTaskState.Lepton.SoftwareVersion.dsp_build);

    ESP_LOGI(TAG, "Part number: %s", DeviceInfo.PartNumber);
    ESP_LOGI(TAG, "Serial number: %s", DeviceInfo.SerialNumber);
    ESP_LOGI(TAG, "GPP revision: %s", DeviceInfo.SoftwareRevision.GPP_Revision);
    ESP_LOGI(TAG, "DSP revision: %s", DeviceInfo.SoftwareRevision.DSP_Revision);

    esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_READY, &DeviceInfo, sizeof(App_Lepton_Device_t),
                   pdMS_TO_TICKS(500));

    while (_LeptonTaskState.ApplicationStarted == false) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGD(TAG, "Start image capturing...");

    Lepton_LoadSettings();

    if (Lepton_StartCapture(&_LeptonTaskState.Lepton, _LeptonTaskState.RawFrameQueue) != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Can not start image capturing!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_FAIL);

        esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_CAMERA_ERROR, NULL, 0, pdMS_TO_TICKS(500));

        _LeptonTaskState.IsRunning = false;
        _LeptonTaskState.TaskHandle = NULL;

        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);

        return;
    }

    while (_LeptonTaskState.IsRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Wait for a new raw frame with longer timeout to avoid busy waiting */
        if (xQueueReceive(_LeptonTaskState.RawFrameQueue, &_LeptonTaskState.RawFrame, pdMS_TO_TICKS(500)) == pdTRUE) {
            uint8_t WriteBufferIdx;
            uint8_t *WriteBuffer;
            int16_t Min = 0;
            int16_t Max = 0;
            Lepton_Telemetry_t Telemetry;
            Lepton_VideoFormat_t VideoFormat;

            if (_LeptonTaskState.RawFrame.Telemetry_Buffer != NULL) {
                memcpy(&Telemetry, _LeptonTaskState.RawFrame.Telemetry_Buffer, sizeof(Lepton_Telemetry_t));
                ESP_LOGD(TAG, "Telemetry - FrameCounter: %u, FPA_Temp: %uK, Housing_Temp: %uK",
                         Telemetry.FrameCounter,
                         Telemetry.FPA_Temp,
                         Telemetry.Housing_Temp);
            }

            ESP_LOGD(TAG, "Processing frame...");

            /* Determine which buffer to write to (ping-pong) */
            if (xSemaphoreTake(_LeptonTaskState.BufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                /* Find a buffer that's not currently being read */
                WriteBufferIdx = (_LeptonTaskState.CurrentReadBuffer + 1) % 2;
                WriteBuffer = _LeptonTaskState.RGB_Buffer[WriteBufferIdx];
                xSemaphoreGive(_LeptonTaskState.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer selection!");

                continue;
            }

            /* Process frame based on video format */
            if (Lepton_GetVideoFormat(&_LeptonTaskState.Lepton, &VideoFormat) != LEPTON_ERR_OK) {
                ESP_LOGE(TAG, "Failed to get video format!");

                continue;
            }

            if (VideoFormat == LEPTON_FORMAT_RGB888) {
                /* RGB888: Data is already in RGB format, just copy it */
                size_t ImageSize = _LeptonTaskState.RawFrame.Width * _LeptonTaskState.RawFrame.Height *
                                   _LeptonTaskState.RawFrame.BytesPerPixel;

                memcpy(WriteBuffer, _LeptonTaskState.RawFrame.Image_Buffer, ImageSize);

                ESP_LOGD(TAG, "Copied RGB888 frame: %ux%u (%u bytes)", _LeptonTaskState.RawFrame.Width,
                         _LeptonTaskState.RawFrame.Height, static_cast<unsigned int>(ImageSize));
            } else {
                /* RAW14: Convert to RGB */
                Lepton_Raw14ToRGB(&_LeptonTaskState.Lepton, _LeptonTaskState.RawFrame.Image_Buffer, WriteBuffer, &Min, &Max,
                                  _LeptonTaskState.RawFrame.Width,
                                  _LeptonTaskState.RawFrame.Height);
            }

            /* Mark buffer as ready and update read buffer index */
            if (xSemaphoreTake(_LeptonTaskState.BufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                _LeptonTaskState.CurrentReadBuffer = WriteBufferIdx;
                xSemaphoreGive(_LeptonTaskState.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer ready!");

                continue;
            }

            /* If UVC streaming is active, also send frames to USB */
            if (_LeptonTaskState.IsUVCStreaming) {
                /* Double-check streaming state to avoid submitting after host closed stream */
                if (USBUVC_IsStreaming() == false) {
                    _LeptonTaskState.IsUVCStreaming = false;

                    ESP_LOGI(TAG, "UVC streaming ended - resuming GUI frames");
                } else {
                    uint8_t *p_JpegData = NULL;
                    size_t JpegSize = 0;

                    esp_err_t JpegError = JPEGEncoder_Encode(WriteBuffer,
                                                             _LeptonTaskState.RawFrame.Width,
                                                             _LeptonTaskState.RawFrame.Height,
                                                             80,
                                                             &p_JpegData,
                                                             &JpegSize);
                    if (JpegError == ESP_OK) {
                        esp_err_t UVCError;
                        
                        UVCError = USBUVC_SubmitFrame(p_JpegData, JpegSize);
                        if (UVCError == ESP_ERR_INVALID_STATE) {
                            /* Streaming was stopped - immediately stop submitting.
                               Next frame iteration will route to GUI path. */
                            _LeptonTaskState.IsUVCStreaming = false;

                            ESP_LOGI(TAG, "UVC stream no longer active - resuming GUI frames");
                        } else if (UVCError != ESP_OK) {
                            ESP_LOGW(TAG, "Failed to submit frame to UVC: 0x%X", UVCError);
                        } else {
                            ESP_LOGD(TAG, "UVC frame submitted: %zu bytes JPEG", JpegSize);
                        }

                        heap_caps_free(p_JpegData);
                    } else {
                        ESP_LOGW(TAG, "JPEG encoding failed: 0x%X", JpegError);
                    }
                }
            }

            App_Lepton_Frame_t FrameEvent = {
                .Buffer = WriteBuffer,
                .Width = _LeptonTaskState.RawFrame.Width,
                .Height = _LeptonTaskState.RawFrame.Height,
                .Channels = 3,
                .Min = Min,
                .Max = Max
            };

            xQueueOverwrite(App_Context->Lepton_FrameQueue, &FrameEvent);
            ESP_LOGD(TAG, "Frame sent to queue successfully");
        } else {
            ESP_LOGW(TAG, "No raw frame received from VoSPI");
        }

        EventBits = xEventGroupGetBits(_LeptonTaskState.EventGroup);
        if (EventBits & LEPTON_TASK_STOP_REQUEST) {
            ESP_LOGI(TAG, "Stop request received");

            _LeptonTaskState.IsRunning = false;

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & LEPTON_TASK_UPDATE_ROI_REQUEST) {
            Lepton_ROI_t ROI;
            Lepton_Error_t Error;

            ROI.Start_Col = _LeptonTaskState.ROI.x;
            ROI.Start_Row = _LeptonTaskState.ROI.y;
            ROI.End_Col = _LeptonTaskState.ROI.x + _LeptonTaskState.ROI.w - 1;
            ROI.End_Row = _LeptonTaskState.ROI.y + _LeptonTaskState.ROI.h - 1;

            switch (_LeptonTaskState.ROI.Type) {
                case ROI_TYPE_SPOTMETER: {
                    Error = Lepton_SetSpotmeterROI(&_LeptonTaskState.Lepton, &ROI);

                    break;
                }
                case ROI_TYPE_SCENE: {
                    Error = Lepton_SetSceneROI(&_LeptonTaskState.Lepton, &ROI);

                    break;
                }
                case ROI_TYPE_AGC: {
                    Error = Lepton_SetAGCROI(&_LeptonTaskState.Lepton, &ROI);

                    break;
                }
                case ROI_TYPE_VIDEO_FOCUS: {
                    Error = Lepton_SetVideoFocusROI(&_LeptonTaskState.Lepton, &ROI);

                    break;
                }
                default: {
                    ESP_LOGW(TAG, "Invalid ROI type in GUI event: 0x%X", _LeptonTaskState.ROI.Type);

                    return;
                }
            }

            if (Error == LEPTON_ERR_OK) {
                ESP_LOGD(TAG, "New Lepton ROI (Type %d) - Start_Col: %u, Start_Row: %u, End_Col: %u, End_Row: %u",
                         _LeptonTaskState.ROI.Type,
                         ROI.Start_Col,
                         ROI.Start_Row,
                         ROI.End_Col,
                         ROI.End_Row);
            } else {
                ESP_LOGE(TAG, "Failed to update Lepton ROI with type %d!", _LeptonTaskState.ROI.Type);
                APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, static_cast<esp_err_t>(Error));
            }

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);
        }

        if (EventBits & LEPTON_TASK_UPDATE_TEMP_REQUEST) {
            uint16_t FPA_Temp;
            uint16_t AUX_Temp;
            App_Lepton_Temperatures_t Temperatures;

            Lepton_GetTemperature(&_LeptonTaskState.Lepton, &FPA_Temp, &AUX_Temp);

            Temperatures.FPA = (static_cast<float>(FPA_Temp) * 0.01f) - 273.15f;
            Temperatures.AUX = (static_cast<float>(AUX_Temp) * 0.01f) - 273.15f;
            esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_FPA_AUX_TEMP, &Temperatures,
                           sizeof(App_Lepton_Temperatures_t), 0);

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
        }

        if (EventBits & LEPTON_TASK_UPDATE_UPTIME_REQUEST) {
            uint32_t Uptime;

            Uptime = Lepton_GetUptime(&_LeptonTaskState.Lepton);

            esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_UPTIME, &Uptime, sizeof(uint32_t), 0);

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);
        }

        if (EventBits & LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE) {
            int16_t x;
            int16_t y;
            float Temperature;
            Lepton_VideoFormat_t VideoFormat;

            Lepton_GetVideoFormat(&_LeptonTaskState.Lepton, &VideoFormat);
            if (((_LeptonTaskState.RawFrame.Width == 0) || (_LeptonTaskState.RawFrame.Height == 0)) &&
                (VideoFormat != LEPTON_FORMAT_RAW14)) {
                ESP_LOGW(TAG, "Invalid Lepton frame! Cannot get pixel temperature!");

                xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

                continue;
            }

            /* Convert the screen position to the Lepton frame coordinates */
            x = (_LeptonTaskState.ScreenPosition.x * _LeptonTaskState.RawFrame.Width) / _LeptonTaskState.ScreenPosition.Width;
            y = (_LeptonTaskState.ScreenPosition.y * _LeptonTaskState.RawFrame.Height) /
                _LeptonTaskState.ScreenPosition.Height;

            ESP_LOGD(TAG, "Crosshair center in Lepton Frame: (%d,%d), size (%d,%d)", x, y, _LeptonTaskState.RawFrame.Width,
                     _LeptonTaskState.RawFrame.Height);

            if (_LeptonTaskState.RawFrame.Image_Buffer != NULL) {
                Lepton_Error_t LeptonError = Lepton_GetPixelTemperature(&_LeptonTaskState.Lepton,
                                                                        _LeptonTaskState.RawFrame.Image_Buffer[(y * _LeptonTaskState.RawFrame.Width) + x],
                                                                        &Temperature);

                if (LeptonError == LEPTON_ERR_OK) {
                    esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_PIXEL_TEMPERATURE, &Temperature, sizeof(float), 0);
                } else {
                    ESP_LOGW(TAG, "Failed to get pixel temperature: 0x%X", LeptonError);
                }
            } else {
                ESP_LOGW(TAG, "Image buffer is NULL, cannot get pixel temperature");
            }

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
        }

        if (EventBits & LEPTON_TASK_UPDATE_SCENE_STATISTICS) {
            Lepton_SceneStatistics_t SceneStats;

            if (Lepton_GetSceneStatistics(&_LeptonTaskState.Lepton, &SceneStats) == LEPTON_ERR_OK) {
                App_Lepton_ROI_Result_t App_Lepton_Scene;

                App_Lepton_Scene.Min = SceneStats.MinIntensity;
                App_Lepton_Scene.Max = SceneStats.MaxIntensity;
                App_Lepton_Scene.Average = SceneStats.MeanIntensity;

                ESP_LOGD(TAG, "Scene Statistics: Min=%.2f\xC2\xB0""C, Max=%.2f\xC2\xB0""C, Average=%.2f\xC2\xB0""C",
                         App_Lepton_Scene.Min, App_Lepton_Scene.Max, App_Lepton_Scene.Average);

                esp_event_post(LEPTON_TASK_EVENTS, LEPTON_TASK_EVENT_RESPONSE_SCENE_STATISTICS, &App_Lepton_Scene,
                               sizeof(App_Lepton_ROI_Result_t), 0);
            } else {
                ESP_LOGW(TAG, "Failed to read scene statistics!");
            }

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_SCENE_STATISTICS);
        }

        if (EventBits & LEPTON_TASK_UPDATE_EMISSIVITY) {
            Lepton_Error_t Error;

            Error = Lepton_SetEmissivity(&_LeptonTaskState.Lepton,
                                         static_cast<Lepton_Emissivity_t>(_LeptonTaskState.NewSetting.Value));

            if (Error == LEPTON_ERR_OK) {
                ESP_LOGD(TAG, "Updated emissivity to %u", _LeptonTaskState.NewSetting.Value);
            } else {
                ESP_LOGE(TAG, "Failed to update emissivity to %u!", _LeptonTaskState.NewSetting.Value);
                APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, static_cast<esp_err_t>(Error));
            }

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_UPDATE_EMISSIVITY);
        }

        if (EventBits & LEPTON_TASK_TEMPERATURE_STATUS_CHANGED) {
            float Offset;
            float T_Compensated;
            Lepton_FluxLinearParams_t FluxParams;
            Settings_Calibration_t Calibration;

            SettingsManager_GetCalibration(&Calibration);

            Offset = static_cast<float>(Calibration.SensorAtCalibration) - Calibration.RoomTemperature;
            T_Compensated = _LeptonTaskState.TemperatureInfo.TempSensor + Offset;

            Lepton_GetFluxLinearParameters(&_LeptonTaskState.Lepton, &FluxParams);
            ESP_LOGD(TAG,
                     "Flux Linear Parameters - Scene Emissivity: %u, TBkgK: %u, TauWindow: %u, TWindowK: %u, TauAtm: %u, TAtmK: %u, ReflWindow: %u, TReflK: %u",
                     FluxParams.SceneEmissivity,
                     FluxParams.TBkgK,
                     FluxParams.TauWindow,
                     FluxParams.TWindowK,
                     FluxParams.TauAtm,
                     FluxParams.TAtmK,
                     FluxParams.ReflWindow,
                     FluxParams.TReflK);

            /* TBkgK: estimated ambient temperature in Kelvin for Lepton flux linear parameters */
            FluxParams.TBkgK = static_cast<uint32_t>((T_Compensated + 273.15f) * 100);

            Lepton_SetFluxLinearParameters(&_LeptonTaskState.Lepton, &FluxParams);

            ESP_LOGD(TAG,
                     "Temperature status changed - sensor: %.2f\xC2\xB0""C, offset: %.2f\xC2\xB0""C, estimated ambient: %.2f\xC2\xB0""C, TBkgK: %u K",
                     _LeptonTaskState.TemperatureInfo.TempSensor,
                     Offset,
                     T_Compensated,
                     FluxParams.TBkgK);

            xEventGroupClearBits(_LeptonTaskState.EventGroup, LEPTON_TASK_TEMPERATURE_STATUS_CHANGED);
        }
    }

    ESP_LOGD(TAG, "Lepton task shutting down");
    Lepton_Deinit(&_LeptonTaskState.Lepton);

    _LeptonTaskState.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Lepton_Task_Init(void)
{
    size_t BufferSize;

    if (_LeptonTaskState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Lepton Task");
    _LeptonTaskState.CurrentReadBuffer = 0;

    _LeptonTaskState.EventGroup = xEventGroupCreate();
    if (_LeptonTaskState.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    _LeptonTaskState.BufferMutex = xSemaphoreCreateMutex();
    if (_LeptonTaskState.BufferMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_ERR_NO_MEM);

        vEventGroupDelete(_LeptonTaskState.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    _LeptonTaskState.LeptonConf = LEPTON_DEFAULT_CONF;
    LEPTON_ASSIGN_I2C_FUNC(_LeptonTaskState.LeptonConf, NULL, NULL, Lepton_CCI_Write, Lepton_CCI_Read, Lepton_CCI_WriteRead);
    LEPTON_ASSIGN_I2C_HANDLE(_LeptonTaskState.LeptonConf, DevicesManager_GetI2CBusHandle());
    LEPTON_ASSIGN_GPIO_FUNC(_LeptonTaskState.LeptonConf, Lepton_Reset, Lepton_PowerDown);

    /* Allocate RGB buffers - both RAW14 and RGB888 use 160x120 resolution
     * RAW14: 160x120x3 = 57,600 bytes (after conversion to RGB)
     * RGB888: 160x120x3 = 57,600 bytes (native RGB data)
     */
    BufferSize = 160 * 120 * 3;

    _LeptonTaskState.RGB_Buffer[0] = static_cast<uint8_t *>(heap_caps_malloc(BufferSize, MALLOC_CAP_SIMD | MALLOC_CAP_SPIRAM));
    _LeptonTaskState.RGB_Buffer[1] = static_cast<uint8_t *>(heap_caps_malloc(BufferSize, MALLOC_CAP_SIMD | MALLOC_CAP_SPIRAM));

    if ((_LeptonTaskState.RGB_Buffer[0] == NULL) || (_LeptonTaskState.RGB_Buffer[1] == NULL)) {
        ESP_LOGE(TAG, "Can not allocate RGB buffers!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_ERR_NO_MEM);

        if (_LeptonTaskState.RGB_Buffer[0]) {
            heap_caps_free(_LeptonTaskState.RGB_Buffer[0]);
        }

        if (_LeptonTaskState.RGB_Buffer[1]) {
            heap_caps_free(_LeptonTaskState.RGB_Buffer[1]);
        }

        Lepton_Deinit(&_LeptonTaskState.Lepton);
        vSemaphoreDelete(_LeptonTaskState.BufferMutex);
        vEventGroupDelete(_LeptonTaskState.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "RGB buffers allocated: 2 x %u bytes", static_cast<unsigned int>(BufferSize));

    /* Create internal queue to receive raw frames from VoSPI capture task */
    _LeptonTaskState.RawFrameQueue = xQueueCreate(1, sizeof(Lepton_FrameBuffer_t));
    if (_LeptonTaskState.RawFrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create raw frame queue!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_ERR_NO_MEM);

        heap_caps_free(_LeptonTaskState.RGB_Buffer[0]);
        heap_caps_free(_LeptonTaskState.RGB_Buffer[1]);
        Lepton_Deinit(&_LeptonTaskState.Lepton);
        vSemaphoreDelete(_LeptonTaskState.BufferMutex);
        vEventGroupDelete(_LeptonTaskState.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    esp_event_handler_register(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE, on_Devices_Task_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_CALIBRATION_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler, NULL);

    _LeptonTaskState.IsUVCStreaming = false;

    ESP_LOGD(TAG, "Lepton Task initialized");

    _LeptonTaskState.IsInitialized = true;

    return ESP_OK;
}

void Lepton_Task_Deinit(void)
{
    if (_LeptonTaskState.IsInitialized == false) {
        return;
    }

    if (_LeptonTaskState.IsRunning) {
        Lepton_Task_Stop();
    }

    ESP_LOGI(TAG, "Deinitializing Lepton Task");

    if (_LeptonTaskState.EventGroup != NULL) {
        vEventGroupDelete(_LeptonTaskState.EventGroup);
        _LeptonTaskState.EventGroup = NULL;
    }

    esp_event_handler_unregister(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE, on_Devices_Task_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_CALIBRATION_CHANGED, on_Settings_Event_Handler);
    esp_event_handler_unregister(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler);

    Lepton_Deinit(&_LeptonTaskState.Lepton);

    if (_LeptonTaskState.BufferMutex != NULL) {
        vSemaphoreDelete(_LeptonTaskState.BufferMutex);
        _LeptonTaskState.BufferMutex = NULL;
    }

    if (_LeptonTaskState.RGB_Buffer[0] != NULL) {
        heap_caps_free(_LeptonTaskState.RGB_Buffer[0]);
        _LeptonTaskState.RGB_Buffer[0] = NULL;
    }

    if (_LeptonTaskState.RGB_Buffer[1] != NULL) {
        heap_caps_free(_LeptonTaskState.RGB_Buffer[1]);
        _LeptonTaskState.RGB_Buffer[1] = NULL;
    }

    if (_LeptonTaskState.RawFrameQueue != NULL) {
        vQueueDelete(_LeptonTaskState.RawFrameQueue);
        _LeptonTaskState.RawFrameQueue = NULL;
    }

    _LeptonTaskState.IsInitialized = false;
}

esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_LeptonTaskState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_LeptonTaskState.IsRunning) {
        ESP_LOGW(TAG, "Task already Running");

        return ESP_OK;
    }

    _LeptonTaskState.IsRunning = true;

    ESP_LOGD(TAG, "Starting Lepton Task");

    Error = xTaskCreatePinnedToCore(Task_Lepton, "Task_Lepton", CONFIG_LEPTON_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_LEPTON_TASK_PRIO, &_LeptonTaskState.TaskHandle, CONFIG_LEPTON_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Lepton Task: 0x%X!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_LEPTON, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Lepton_Task_Stop(void)
{
    if (_LeptonTaskState.IsRunning == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Lepton Task");

    xEventGroupSetBits(_LeptonTaskState.EventGroup, LEPTON_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Lepton_Task_IsRunning(void)
{
    return _LeptonTaskState.IsRunning;
}