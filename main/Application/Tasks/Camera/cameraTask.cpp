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
#include <esp_camera.h>
#include <esp_camera_af.h>
#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string.h>
#include <stdbool.h>

#include "cameraTask.h"
#include "Application/application.h"
#include "AppDiag/appDiag.h"

#define CAMERA_TASK_STOP_REQUEST           BIT0
#define CAMERA_TASK_FOCUS_REQUEST          BIT1

ESP_EVENT_DEFINE_BASE(CAMERA_TASK_EVENTS);

static camera_config_t _CameraConfig = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = -1,
    .pin_sccb_sda = -1,
    .pin_sccb_scl = -1,
    .pin_d7 = CONFIG_CAMERA_PIN_D7,
    .pin_d6 = CONFIG_CAMERA_PIN_D6,
    .pin_d5 = CONFIG_CAMERA_PIN_D5,
    .pin_d4 = CONFIG_CAMERA_PIN_D4,
    .pin_d3 = CONFIG_CAMERA_PIN_D3,
    .pin_d2 = CONFIG_CAMERA_PIN_D2,
    .pin_d1 = CONFIG_CAMERA_PIN_D1,
    .pin_d0 = CONFIG_CAMERA_PIN_D0,
    .pin_vsync = CONFIG_CAMERA_PIN_VSYNC,
    .pin_href = CONFIG_CAMERA_PIN_HREF,
    .pin_pclk = CONFIG_CAMERA_PIN_PCLK,
    .xclk_freq_hz = 25000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 10,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,

#if defined(CONFIG_DEVICES_I2C_I2C0_HOST)
    .sccb_i2c_port = I2C_NUM_0,
#elif defined(CONFIG_DEVICES_I2C_I2C1_HOST)
    .sccb_i2c_port = I2C_NUM_1,
#else
#error "No I2C host defined for camera!"
#endif
};

/** @brief Internal runtime state of the camera task.
 *         Holds all FreeRTOS primitives required to manage the visible-light camera
 *         hardware initialisation sequence and the running task lifecycle.
 */
typedef struct {
    bool IsInitialized;                 /**< true after Camera_Task_Init() has completed successfully. */
    bool IsRunning;                     /**< true while the FreeRTOS task is executing. */
    TaskHandle_t TaskHandle;            /**< FreeRTOS task handle; NULL before Camera_Task_Start(). */
    EventGroupHandle_t EventGroup;      /**< Event group used for intra-task synchronisation. */
    sensor_t *Sensor;                   /**< Pointer to the camera sensor object, obtained from esp_camera_sensor_get() after initialisation. */
    uint8_t *p_FrameBuffer;             /**< PSRAM frame buffer for one RGB565 QVGA frame (320 x 240 x 2 = 153,600 bytes); NULL before init. */
} Camera_Task_State_t;

static Camera_Task_State_t _CameraTaskState;

static const char *TAG = "cameraTask";

/** @brief                  Event handler for the camera task to receive updates when camera events are triggered (e.g., focus requests).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Camera_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Camera task event received: ID=%d", ID);

    switch (ID) {
        case CAMERA_TASK_EVENT_REQUEST_FOCUS: {
            ESP_LOGD(TAG, "Focus request event received");

            xEventGroupSetBits(_CameraTaskState.EventGroup, CAMERA_TASK_FOCUS_REQUEST);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled camera task event ID: 0x%X", ID);

            break;
        }
    }
}

/** @brief              Camera task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Camera(void *p_Parameters)
{
    esp_err_t Error;
    esp_camera_af_config_t AutofocusConfig = {
        .mode = ESP_CAMERA_AF_MODE_MANUAL,
        .step_size = 0,
        .range_min = 0,
        .range_max = 0,
        .timeout_ms = CONFIG_CAMERA_AF_DEFAULT_TIMEOUT_MS,
    };

    ESP_LOGD(TAG, "Camera task started on core %d", xPortGetCoreID());

    if (esp_camera_af_is_supported(_CameraTaskState.Sensor) == false) {
        ESP_LOGE(TAG, "AF not supported by this sensor");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, ESP_ERR_NOT_SUPPORTED);
    }

    Error = esp_camera_af_init(_CameraTaskState.Sensor, &AutofocusConfig);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "AF init failed: 0x%x!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, Error);
    } else {
        ESP_LOGD(TAG, "AF initialized (MANUAL mode)");
    }

    if (Error == ESP_OK) {
        _CameraTaskState.IsInitialized = true;

        ESP_LOGI(TAG, "Camera initialized");

        esp_event_post(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_COMPLETE, NULL, 0, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, Error);

        esp_event_post(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_FAILED, &Error, sizeof(Error), portMAX_DELAY);
    }

    esp_task_wdt_add(NULL);

    while (_CameraTaskState.IsRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_CameraTaskState.EventGroup);
        if (EventBits & CAMERA_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _CameraTaskState.IsRunning = false;

            xEventGroupClearBits(_CameraTaskState.EventGroup, CAMERA_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & CAMERA_TASK_FOCUS_REQUEST) {
            ESP_LOGD(TAG, "Focus request received");

            esp_camera_af_trigger(_CameraTaskState.Sensor);

            xEventGroupClearBits(_CameraTaskState.EventGroup, CAMERA_TASK_FOCUS_REQUEST);
        }

        camera_fb_t *Pic = esp_camera_fb_get();
        if (Pic != NULL) {
            /* Copy frame to PSRAM buffer and immediately release the DMA buffer back to the driver. */
            memcpy(_CameraTaskState.p_FrameBuffer, Pic->buf, Pic->len);

            App_Camera_Frame_t Frame = {
                .Buffer = _CameraTaskState.p_FrameBuffer,
                .Width  = Pic->width,
                .Height = Pic->height
            };

            esp_camera_fb_return(Pic);

            xQueueOverwrite(static_cast<App_Context_t *>(p_Parameters)->Camera_FrameQueue, &Frame);
        } else {
            ESP_LOGW(TAG, "Failed to get camera frame buffer");
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }

    ESP_LOGD(TAG, "Camera task shutting down");

    _CameraTaskState.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Camera_Task_Init(void)
{
    esp_err_t Error;

    if (_CameraTaskState.EventGroup != NULL) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    _CameraTaskState.EventGroup = xEventGroupCreate();
    if (_CameraTaskState.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    /* RGB565 QVGA frame buffer: 320 x 240 x 2 = 153,600 bytes (PSRAM) */
    _CameraTaskState.p_FrameBuffer = static_cast<uint8_t *>(heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM));
    if (_CameraTaskState.p_FrameBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate camera frame buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, ESP_ERR_NO_MEM);
        vEventGroupDelete(_CameraTaskState.EventGroup);
        _CameraTaskState.EventGroup = NULL;

        return ESP_ERR_NO_MEM;
    }

    Error = esp_camera_init(&_CameraConfig);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize camera: 0x%x!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, Error);
    }

    _CameraTaskState.Sensor = esp_camera_sensor_get();

    esp_event_handler_register(CAMERA_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Camera_Task_Event_Handler, NULL);

    return ESP_OK;
}

void Camera_Task_Deinit(void)
{
    if (_CameraTaskState.IsInitialized == false) {
        return;
    }

    esp_camera_deinit();

    if (_CameraTaskState.EventGroup != NULL) {
        vEventGroupDelete(_CameraTaskState.EventGroup);
        _CameraTaskState.EventGroup = NULL;
    }

    esp_event_handler_unregister(CAMERA_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Camera_Task_Event_Handler);

    if (_CameraTaskState.p_FrameBuffer != NULL) {
        heap_caps_free(_CameraTaskState.p_FrameBuffer);
        _CameraTaskState.p_FrameBuffer = NULL;
    }

    _CameraTaskState.IsInitialized = false;

    return;
}

esp_err_t Camera_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_CameraTaskState.IsRunning) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    _CameraTaskState.IsRunning = true;

    ESP_LOGD(TAG, "Starting Camera Task");

    Error = xTaskCreatePinnedToCore(Task_Camera, "Task_Camera", CONFIG_CAMERA_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_CAMERA_TASK_PRIO, &_CameraTaskState.TaskHandle, CONFIG_CAMERA_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Camera Task: 0x%X!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_CAMERA, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Camera_Task_Stop(void)
{
    if (_CameraTaskState.IsRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Camera Task");

    xEventGroupSetBits(_CameraTaskState.EventGroup, CAMERA_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Camera_Task_IsRunning(void)
{
    return _CameraTaskState.IsRunning;
}