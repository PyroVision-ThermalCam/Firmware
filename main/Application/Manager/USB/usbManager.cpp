/*
 * usbManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Manager implementation - Composite USB device coordinator (MSC + UVC + CDC).
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
#include <freertos/queue.h>

#include <tinyusb.h>
#include <tinyusb_default_config.h>

#include <string.h>

#include "usbManager.h"
#include "MSC/usbMSC.h"
#include "UVC/usbUVC.h"
#include "CDC/usbCDC.h"
#include "../AppDiag/appDiag.h"
#include "Descriptors/descriptors.h"
#include "../Memory/memoryManager.h"

ESP_EVENT_DEFINE_BASE(USB_EVENTS);

/** @brief USB Manager internal command IDs for the task queue.
 */
typedef enum {
    USB_CMD_ENABLE_MSC,                     /**< Initialize and enable USB MSC. */
    USB_CMD_DISABLE_MSC,                    /**< Deinitialize and disable USB MSC. */
    USB_CMD_ENABLE_UVC,                     /**< Initialize and enable USB UVC. */
    USB_CMD_DISABLE_UVC,                    /**< Deinitialize and disable USB UVC. */
} USB_Manager_Cmd_ID_t;

/** @brief USB Manager command structure passed through the internal queue.
 */
typedef struct {
    USB_Manager_Cmd_ID_t ID;                /**< Command identifier. */
} USB_Manager_Cmd_t;

/** @brief USB Manager internal state.
 */
typedef struct {
    bool IsInitialized;                     /**< TinyUSB driver installed and CDC active. */
    bool IsCableConnected;                  /**< USB cable connected and enumerated by host. */
    QueueHandle_t CommandQueue;             /**< Queue for MSC/UVC enable/disable commands. */
    TaskHandle_t MonitoringTask;            /**< Handle for the USB monitoring/command task. */
    const char *StringDescriptors[4];       /**< String descriptor pointers (LangID, Manufacturer, Product, Serial). */
} USB_Manager_State_t;

static USB_Manager_State_t _USBManagerState;

static const char *TAG = "USB-Manager";

/* Language ID descriptor for USB (English US - 0x0409) */
static const char USB_LangID[2] = { 0x09, 0x04 };

/** @brief          USB Manager monitoring and command processing task.
 *                  Polls tud_mounted() every 500 ms to detect cable connect / disconnect
 *                  transitions and posts the corresponding USB event. Also processes
 *                  MSC and UVC enable / disable commands from the internal command queue.
 *  @param p_Arg    Unused task argument.
 */
static void USB_Monitoring_Task(void *p_Arg)
{
    USB_Manager_Cmd_t Cmd;

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "USB monitoring task started");

    while (true) {
        if ((tud_connected() == true) && (_USBManagerState.IsCableConnected == false)) {
            ESP_LOGD(TAG, "USB cable connected (host enumeration complete)");

            _USBManagerState.IsCableConnected = true;

            esp_event_post(USB_EVENTS, USB_EVENT_CABLE_CONNECTED, NULL, 0, pdMS_TO_TICKS(100));
        } else if ((tud_connected() == false) && (_USBManagerState.IsCableConnected == true)) {
            ESP_LOGD(TAG, "USB cable disconnected");

            _USBManagerState.IsCableConnected = false;

            if (USBMSC_IsInitialized()) {
                ESP_LOGD(TAG, "Force-deinit MSC on cable disconnect");
                USBMSC_Deinit();
            }

            if (USBUVC_IsInitialized()) {
                ESP_LOGD(TAG, "Force-deinit UVC on cable disconnect");
                USBUVC_Deinit();
            }

            esp_event_post(USB_EVENTS, USB_EVENT_CABLE_DISCONNECTED, NULL, 0, pdMS_TO_TICKS(100));
        }

        if (xQueueReceive(_USBManagerState.CommandQueue, &Cmd, 0) == pdTRUE) {
            esp_err_t Error;

            switch (Cmd.ID) {
                case USB_CMD_ENABLE_MSC: {
                    if (USBMSC_IsInitialized()) {
                        ESP_LOGW(TAG, "MSC already initialized!");

                        break;
                    }

                    USB_MSC_Config_t MSC_Config = {
                        .MountPoint = MemoryManager_GetStoragePath(),
                    };

                    Error = USBMSC_Init(&MSC_Config);
                    if (Error != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to initialize USB MSC: 0x%X!", Error);
                    } else {
                        ESP_LOGD(TAG, "USB MSC enabled");
                    }

                    break;
                }
                case USB_CMD_DISABLE_MSC: {
                    if (USBMSC_IsInitialized() == false) {
                        ESP_LOGW(TAG, "MSC not active, nothing to disable!");

                        break;
                    }

                    Error = USBMSC_Deinit();
                    if (Error != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to deinitialize USB MSC: 0x%X!", Error);
                    } else {
                        ESP_LOGD(TAG, "USB MSC disabled");
                    }

                    break;
                }
                case USB_CMD_ENABLE_UVC: {
                    USB_UVC_Config_t UVC_Config = {
                        .Width = 160,
                        .Height = 120,
                        .FrameRate = 9,
                    };

                    if (USBUVC_IsInitialized()) {
                        ESP_LOGW(TAG, "UVC already initialized!");

                        break;
                    }

                    Error = USBUVC_Init(&UVC_Config);
                    if (Error != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to initialize USB UVC: 0x%X!", Error);
                    } else {
                        ESP_LOGD(TAG, "USB UVC enabled");
                    }

                    break;
                }
                case USB_CMD_DISABLE_UVC: {
                    if (USBUVC_IsInitialized() == false) {
                        ESP_LOGW(TAG, "UVC not active, nothing to disable!");

                        break;
                    }

                    Error = USBUVC_Deinit();
                    if (Error != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to deinitialize USB UVC: 0x%X!", Error);
                    } else {
                        ESP_LOGD(TAG, "USB UVC disabled");
                    }

                    break;
                }
                default: {
                    ESP_LOGW(TAG, "Unknown USB Manager command: 0x%X", static_cast<int>(Cmd.ID));

                    break;
                }
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t USBManager_Init(void)
{
    esp_err_t USB_Error;
    BaseType_t Error;
    tinyusb_config_t USB_Config = TINYUSB_DEFAULT_CONFIG();
    USB_CDC_Config_t CDC_Config = {
        .Reserved = 0,
    };

    if (_USBManagerState.IsInitialized) {
        ESP_LOGW(TAG, "USB Manager already initialized!");

        return USB_ERR_ALREADY_INITIALIZED;
    }

    ESP_LOGD(TAG, "Initializing USB Manager...");

    memset(&_USBManagerState, 0, sizeof(USB_Manager_State_t));

    _USBManagerState.StringDescriptors[0] = USB_LangID;
    _USBManagerState.StringDescriptors[1] = CONFIG_DEVICE_MANUFACTURER;
    _USBManagerState.StringDescriptors[2] = CONFIG_DEVICE_NAME;
    _USBManagerState.StringDescriptors[3] = NULL;

    ESP_LOGD(TAG, "USB Descriptors: VID:PID = 0x%04X:0x%04X",
             get_Desc_Device()->idVendor,
             get_Desc_Device()->idProduct);
    ESP_LOGD(TAG, "  Manufacturer: %s, Product: %s",
             _USBManagerState.StringDescriptors[1], _USBManagerState.StringDescriptors[2]);

    USB_Config.descriptor.device = get_Desc_Device();
    USB_Config.descriptor.full_speed_config = get_Desc_Config();
    USB_Config.descriptor.string = _USBManagerState.StringDescriptors;
    USB_Config.descriptor.string_count = 3;

    ESP_LOGD(TAG, "Installing TinyUSB driver...");
    USB_Error = tinyusb_driver_install(&USB_Config);
    if (USB_Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: 0x%X!", USB_Error);

        APP_DIAG_RECORD(APP_DIAG_SOURCE_USB, USB_ERR_DRIVER_INSTALL);

        return USB_ERR_DRIVER_INSTALL;
    }

    USB_Error = USBCDC_Init(&CDC_Config);
    if (USB_Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB CDC: 0x%X!", USB_Error);

        tinyusb_driver_uninstall();

        APP_DIAG_RECORD(APP_DIAG_SOURCE_USB, USB_ERR_CDC_INIT);

        return USB_ERR_CDC_INIT;
    }

    _USBManagerState.CommandQueue = xQueueCreate(8, sizeof(USB_Manager_Cmd_t));
    if (_USBManagerState.CommandQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create USB command queue!");

        USBCDC_Deinit();
        tinyusb_driver_uninstall();

        APP_DIAG_RECORD(APP_DIAG_SOURCE_USB, USB_ERR_QUEUE_CREATE);

        return USB_ERR_QUEUE_CREATE;
    }

    Error = xTaskCreate(USB_Monitoring_Task, "USBMonTask", 4096, NULL, 5, &_USBManagerState.MonitoringTask);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB monitoring task: 0x%X!", Error);

        vQueueDelete(_USBManagerState.CommandQueue);
        _USBManagerState.CommandQueue = NULL;

        USBCDC_Deinit();
        tinyusb_driver_uninstall();

        APP_DIAG_RECORD(APP_DIAG_SOURCE_USB, USB_ERR_TASK_CREATE);

        return USB_ERR_TASK_CREATE;
    }

    tud_connect();

    _USBManagerState.IsInitialized = true;

    esp_event_post(USB_EVENTS, USB_EVENT_INITIALIZED, NULL, 0, pdMS_TO_TICKS(100));

    ESP_LOGD(TAG, "USB Manager initialized successfully");

    return ESP_OK;
}

esp_err_t USBManager_Deinit(void)
{
    esp_err_t Error;

    if (_USBManagerState.IsInitialized == false) {
        ESP_LOGW(TAG, "USB Manager not initialized");

        return USB_ERR_NOT_INITIALIZED;
    }

    ESP_LOGD(TAG, "Deinitializing USB Manager...");

    if (_USBManagerState.MonitoringTask != NULL) {
        vTaskDelete(_USBManagerState.MonitoringTask);
        _USBManagerState.MonitoringTask = NULL;
    }

    if (_USBManagerState.CommandQueue != NULL) {
        vQueueDelete(_USBManagerState.CommandQueue);
        _USBManagerState.CommandQueue = NULL;
    }

    _USBManagerState.IsInitialized = false;
    _USBManagerState.IsCableConnected = false;

    ESP_LOGD(TAG, "Disconnecting from USB bus...");
    if (tud_disconnect() == false) {
        ESP_LOGW(TAG, "Failed to initiate USB disconnect");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    Error = USBCDC_Deinit();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize USB CDC: 0x%X!", Error);
    } else {
        ESP_LOGD(TAG, "USB CDC deinitialized");
    }

    Error = USBUVC_Deinit();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize USB UVC: 0x%X!", Error);
    } else {
        ESP_LOGD(TAG, "USB UVC deinitialized");
    }

    Error = USBMSC_Deinit();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize USB MSC: 0x%X!", Error);
    } else {
        ESP_LOGD(TAG, "USB MSC deinitialized");
    }

    Error = tinyusb_driver_uninstall();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to uninstall TinyUSB driver: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "TinyUSB driver uninstalled");

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_event_post(USB_EVENTS, USB_EVENT_UNINITIALIZED, NULL, 0, pdMS_TO_TICKS(100));

    ESP_LOGD(TAG, "USB Manager deinitialized successfully");

    return ESP_OK;
}

esp_err_t USBManager_EnableMSC(bool Enable)
{
    USB_Manager_Cmd_t Cmd = {
        .ID = Enable ? USB_CMD_ENABLE_MSC : USB_CMD_DISABLE_MSC,
    };

    if (_USBManagerState.IsInitialized == false) {
        return USB_ERR_NOT_INITIALIZED;
    }

    if (xQueueSend(_USBManagerState.CommandQueue, &Cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "USB command queue full, EnableMSC command dropped!");

        return USB_ERR_QUEUE_FULL;
    }

    return ESP_OK;
}

esp_err_t USBManager_EnableUVC(bool Enable)
{
    USB_Manager_Cmd_t Cmd = {
        .ID = Enable ? USB_CMD_ENABLE_UVC : USB_CMD_DISABLE_UVC,
    };

    if (_USBManagerState.IsInitialized == false) {
        return USB_ERR_NOT_INITIALIZED;
    }

    if (xQueueSend(_USBManagerState.CommandQueue, &Cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "USB command queue full, EnableUVC command dropped!");

        return USB_ERR_QUEUE_FULL;
    }

    return ESP_OK;
}

bool USBManager_IsInitialized(void)
{
    return _USBManagerState.IsInitialized;
}

bool USBManager_IsCableConnected(void)
{
    return _USBManagerState.IsCableConnected;
}