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
#include <wear_levelling.h>

#include <nvs_flash.h>

#include "managers.h"
#include "Application/Tasks/tasks.h"
#include "Application/application.h"

static App_Context_t _App_Context;

static const char *TAG = "main";

/** @brief Main application entry point.
 *         Initializes all managers, tasks, and starts the application.
 */
extern "C" void app_main(void)
{
    i2c_master_dev_handle_t RtcHandle = NULL;

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    _App_Context.Lepton_FrameEventQueue = xQueueCreate(1, sizeof(App_Lepton_FrameReady_t));
    if (_App_Context.Lepton_FrameEventQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue!");

        return;
    }

    ESP_ERROR_CHECK(SettingsManager_Init());
    ESP_ERROR_CHECK(Devices_Task_Init());
    ESP_ERROR_CHECK(MemoryManager_Init());

    /* Initialize Time Manager (requires RTC from DevicesManager) */
    if (DevicesManager_GetRTCHandle(&RtcHandle) == ESP_OK) {
        if (TimeManager_Init(RtcHandle) == ESP_OK) {
            App_Settings_System_t SystemSettings;

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
    ESP_ERROR_CHECK(Camera_Task_Init());
    ESP_ERROR_CHECK(Network_Task_Init(&_App_Context));
    ESP_LOGI(TAG, " Initialization successful");

    ESP_LOGI(TAG, "Starting tasks...");
    ESP_ERROR_CHECK(Devices_Task_Start(&_App_Context));
    ESP_ERROR_CHECK(GUI_Task_Start(&_App_Context));
    ESP_ERROR_CHECK(Lepton_Task_Start(&_App_Context));
    ESP_ERROR_CHECK(Network_Task_Start());
    ESP_LOGI(TAG, " Tasks started");

    /* Main task can now be deleted - no need to remove from watchdog as it was never added */
    vTaskDelete(NULL);
}