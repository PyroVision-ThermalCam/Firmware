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

/* USB Test Mode - Enable to test USB Mass Storage Device */
#define USB_TEST_MODE_ENABLED 0  /* Disabled - allows normal app operation */

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
    ESP_ERROR_CHECK(DevicesTask_Init());
    ESP_ERROR_CHECK(MemoryManager_Init());

#if USB_TEST_MODE_ENABLED
    /* USB Test Mode - Initialize USB MSC and halt execution */
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, " USB TEST MODE ENABLED");
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "The device will expose storage via USB Mass Storage.");
    ESP_LOGI(TAG, "Normal application will NOT start.");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "To disable test mode:");
    ESP_LOGI(TAG, "  Set USB_TEST_MODE_ENABLED to 0 in main.cpp");
    ESP_LOGI(TAG, "");

    /* Get wear leveling handle for internal storage */
    wl_handle_t WL_Handle;
    esp_err_t Error = MemoryManager_GetWearLevelingHandle(&WL_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get wear leveling handle: %d", Error);
        ESP_LOGI(TAG, "Continuing with normal operation...");
    } else {
        /* Configure USB Manager - storage type is auto-detected */
        USB_Manager_Config_t USB_Config = {
            .p_MountPoint = "/storage",
            .p_VendorID = "PyroVis",
            .p_ProductID = "ThermalCam",
            .p_ProductRevision = "1.0",
        };

        ESP_LOGI(TAG, "Using internal flash storage: %s", MemoryManager_GetStoragePath());
        ESP_LOGI(TAG, "Wear Leveling Handle: %d", WL_Handle);
        ESP_LOGI(TAG, "Initializing USB Mass Storage Device...");

        Error = USBManager_Init(&USB_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB Manager: %d", Error);
            ESP_LOGI(TAG, "Continuing with normal operation...");
        } else {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "✓ USB Mass Storage Device active!");
            ESP_LOGI(TAG, "✓ Internal flash exposed via USB");
            ESP_LOGI(TAG, "✓ Connect USB cable to PC");
            ESP_LOGI(TAG, "✓ Device should appear as removable drive");
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "⚠️  WARNING: Filesystem is now under PC control.");
            ESP_LOGI(TAG, "⚠️  Do NOT reset device without safely ejecting from PC!");
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "Entering infinite loop...");

            /* Infinite loop - device stays in USB mode */
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
#endif

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

    //SDManager_Init();

    ESP_ERROR_CHECK(GUI_Task_Init());
    ESP_ERROR_CHECK(Lepton_Task_Init());
    ESP_ERROR_CHECK(Network_Task_Init(&_App_Context));
    ESP_LOGI(TAG, " Initialization successful");

    ESP_LOGI(TAG, "Starting tasks...");
    ESP_ERROR_CHECK(DevicesTask_Start(&_App_Context));
    ESP_ERROR_CHECK(GUI_Task_Start(&_App_Context));
    ESP_ERROR_CHECK(Lepton_Task_Start(&_App_Context));
    ESP_ERROR_CHECK(Network_Task_Start());
    ESP_LOGI(TAG, " Tasks started");

    /* Main task can now be deleted - no need to remove from watchdog as it was never added */
    vTaskDelete(NULL);
}