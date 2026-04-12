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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string.h>
#include <stdbool.h>

#include "cameraTask.h"
#include "Application/application.h"

#define CAMERA_TASK_STOP_REQUEST           BIT0

ESP_EVENT_DEFINE_BASE(CAMERA_EVENTS);

static const camera_config_t _Camera_Config = {
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

typedef struct {
    bool isInitialized;
    bool isRunning;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
} Camera_Task_State_t;

static Camera_Task_State_t _Camera_Task_State;

static const char *TAG = "cameraTask";

/** @brief              Camera task main loop. Performs hardware initialisation on entry,
 *                      then posts CAMERA_EVENT_INIT_COMPLETE or CAMERA_EVENT_INIT_FAILED
 *                      to the default event loop before entering the main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Camera(void *p_Parameters)
{
    esp_err_t Error;
    sensor_t *s;
    esp_camera_af_config_t Autofocus_Config = {
        .mode = ESP_CAMERA_AF_MODE_AUTO,
        .step_size = 0,
        .range_min = 0,
        .range_max = 0,
        .timeout_ms = CONFIG_CAMERA_AF_DEFAULT_TIMEOUT_MS,
    };

    ESP_LOGD(TAG, "Camera task started on core %d", xPortGetCoreID());

    Error = esp_camera_init(&_Camera_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize camera: 0x%x!", Error);
    }

    s = esp_camera_sensor_get();

    if (esp_camera_af_is_supported(s) == false) {
        ESP_LOGI(TAG, "AF: not supported by this sensor");
    }

    Error = esp_camera_af_init(s, &Autofocus_Config);
    if (Error != ESP_OK) {
        /* AF init failure is non-fatal. Most likely cause: OV5640 module is fixed-focus
         * (no VCM actuator). The internal MCU firmware is loaded but the actuator is
         * missing, causing a timeout in ov5640_af_wait_fw_idle(). */
        ESP_LOGW(TAG, "AF init failed (%d) - fixed-focus module? Continuing without AF.", Error);
    } else {
        ESP_LOGI(TAG, "AF initialized (AUTO mode)");
    }

    if (Error == ESP_OK) {
        _Camera_Task_State.isInitialized = true;
        esp_event_post(CAMERA_EVENTS, CAMERA_EVENT_INIT_COMPLETE, NULL, 0, portMAX_DELAY);

        ESP_LOGI(TAG, "Camera initialized");
    } else {
        esp_event_post(CAMERA_EVENTS, CAMERA_EVENT_INIT_FAILED, &Error, sizeof(Error), portMAX_DELAY);

        ESP_LOGE(TAG, "Camera init failed: 0x%x", Error);
    }

    esp_task_wdt_add(NULL);

    while (_Camera_Task_State.isRunning) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "Camera task shutting down");

    _Camera_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Camera_Task_Init(void)
{
    if (_Camera_Task_State.EventGroup != NULL) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    esp_camera_deinit();

    _Camera_Task_State.EventGroup = xEventGroupCreate();
    if (_Camera_Task_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void Camera_Task_Deinit(void)
{
    if (_Camera_Task_State.isInitialized == false) {
        return;
    }

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
    } else if (_Camera_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    _Camera_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Camera Task");

    Error = xTaskCreatePinnedToCore(Task_Camera, "Task_Camera", CONFIG_CAMERA_TASK_STACKSIZE, p_AppContext,
                                    CONFIG_CAMERA_TASK_PRIO, &_Camera_Task_State.TaskHandle, CONFIG_CAMERA_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Camera Task: 0x%X!", Error);

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