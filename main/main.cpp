/*
 * main.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Main application entry point.
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
 */

#include <esp_log.h>
#include <esp_event.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "managers.h"
#include "Application/application.h"
#include "Application/Tasks/GUI/guiTask.h"
#include "Application/Tasks/Camera/cameraTask.h"
#include "Application/Tasks/Lepton/leptonTask.h"
#include "Application/Tasks/Network/networkTask.h"
#include "Application/Tasks/Devices/devicesTask.h"

#define APP_INIT_TASK_STACKSIZE     16384
#define APP_INIT_TASK_PRIORITY      5
#define APP_INIT_TASK_CORE          0

static App_Context_t _AppContext;

static const char *TAG = "main";

/** @brief  FreeRTOS task that performs the full application initialization.
 *          Spawned by app_main() so that the ESP-IDF main task (whose stack
 *          size is controlled by CONFIG_ESP_MAIN_TASK_STACK_SIZE) can remain
 *          small. Deletes itself once all managers and tasks have been started.
 *  @param  p_Args  Unused task argument.
 */
static void run_app_init(void *p_Args)
{
    RV8263C8_Dev_t RtcHandle = {};

    _AppContext.Lepton_FrameQueue = xQueueCreate(1, sizeof(App_Lepton_Frame_t));
    if (_AppContext.Lepton_FrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create Lepton frame queue!");

        vTaskDelete(NULL);
        return;
    }

    _AppContext.Camera_FrameQueue = xQueueCreate(1, sizeof(App_Camera_Frame_t));
    if (_AppContext.Camera_FrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create camera frame queue!");

        vTaskDelete(NULL);
        return;
    }

    _AppContext.InputMutex = xSemaphoreCreateMutex();
    if (_AppContext.InputMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create input mutex!");

        vTaskDelete(NULL);
        return;
    }

    memset(&_AppContext.InputState, 0, sizeof(DevicesManager_Input_State_t));

    ESP_ERROR_CHECK(SettingsManager_Init());
    ESP_ERROR_CHECK(Devices_Task_Init());
    ESP_ERROR_CHECK(MemoryManager_Init());
    ESP_ERROR_CHECK(USBManager_Init());

    if (DevicesManager_GetRTCHandle(&RtcHandle) == ESP_OK) {
        if (TimeManager_Init(&RtcHandle) == ESP_OK) {
            Settings_System_t SystemSettings;

            SettingsManager_GetSystem(&SystemSettings);
            TimeManager_SetTimezone(SystemSettings.Timezone);
        } else {
            ESP_LOGW(TAG, "Failed to initialize Time Manager!");
        }
    } else {
        ESP_LOGW(TAG, "RTC not available, Time Manager initialization skipped");
    }

    ESP_ERROR_CHECK(GUI_Task_Init());
    ESP_ERROR_CHECK(Lepton_Task_Init());
    ESP_ERROR_CHECK(Network_Task_Init(&_AppContext));
    ESP_ERROR_CHECK(Camera_Task_Init());
    ESP_LOGI(TAG, "Initialization successful");

    ESP_LOGI(TAG, "Starting tasks...");
    ESP_ERROR_CHECK(Devices_Task_Start(&_AppContext));
    ESP_ERROR_CHECK(GUI_Task_Start(&_AppContext));
    ESP_ERROR_CHECK(Lepton_Task_Start(&_AppContext));
    ESP_ERROR_CHECK(Camera_Task_Start(&_AppContext));
    ESP_ERROR_CHECK(Network_Task_Start());
    ESP_LOGI(TAG, "Tasks started");

    vTaskDelete(NULL);
}

/** @brief  Main application entry point.
 *          Creates the application initialisation task and returns immediately,
 *          keeping the ESP-IDF main task stack usage minimal.
 */
extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreatePinnedToCore(run_app_init, "AppInit", APP_INIT_TASK_STACKSIZE, NULL, APP_INIT_TASK_PRIORITY, NULL, APP_INIT_TASK_CORE);
}