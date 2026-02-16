/*
 * usbManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Manager implementation - Main coordinator for USB MSC module.
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

#include <tinyusb.h>
#include <tinyusb_default_config.h>

#include <string.h>

#include "usbManager.h"
#include "descriptors.h"
#include "MSC/usbMSC.h"
#include "UVC/usbUVC.h"

ESP_EVENT_DEFINE_BASE(USB_EVENTS);

/** @brief USB Manager internal state.
 */
typedef struct {
    bool isInitialized;                     /**< Initialization state. */
    bool isUSBMounted;                      /**< USB host has mounted device. */
    USB_Mode_t Mode;                        /**< Current USB mode. */
    tusb_desc_device_t DeviceDescriptor;    /**< Custom device descriptor. */
    const char *StringDescriptors[3];       /**< String descriptor pointers (0-2: LangID, Manufacturer, Product). */
} USB_Manager_State_t;

static USB_Manager_State_t _USB_Manager_State;

static const char *TAG = "USB-Manager";

/* Language ID descriptor for USB (English US - 0x0409) */
static const char USB_LangID[2] = {0x09, 0x04};

esp_err_t USBManager_Init(const USB_Manager_Config_t *p_Config)
{
    esp_err_t Error;
    tinyusb_config_t USB_Config = TINYUSB_DEFAULT_CONFIG();

    if (p_Config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration pointer!");

        return ESP_ERR_INVALID_ARG;
    } else if (_USB_Manager_State.isInitialized) {
        ESP_LOGW(TAG, "USB Manager already initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Validate mode-specific parameters */
    if (p_Config->Mode == USB_MODE_MSC) {
        if (p_Config->MountPoint == NULL) {
            ESP_LOGE(TAG, "MSC mode requires valid MountPoint!");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "Initializing USB Manager in MSC mode...");
    } else if (p_Config->Mode == USB_MODE_UVC) {
        ESP_LOGI(TAG, "Initializing USB Manager in UVC mode...");
    } else {
        ESP_LOGE(TAG, "Unsupported USB mode: %d", p_Config->Mode);
        return ESP_ERR_NOT_SUPPORTED;
    }

    memset(&_USB_Manager_State, 0, sizeof(USB_Manager_State_t));

    /* Store mode */
    _USB_Manager_State.Mode = p_Config->Mode;

    /* Configure the Device Descriptor */
    memcpy(&_USB_Manager_State.DeviceDescriptor, get_Desc_Device(), sizeof(tusb_desc_device_t));

    /* Set up string descriptors */
    _USB_Manager_State.StringDescriptors[0] = USB_LangID;                   // 0: Language ID (English US - 0x0409)
    _USB_Manager_State.StringDescriptors[1] = CONFIG_DEVICE_MANUFACTURER;   // 1: Manufacturer
    _USB_Manager_State.StringDescriptors[2] = CONFIG_DEVICE_NAME;           // 2: Product

    ESP_LOGD(TAG, "USB Descriptors:");
    ESP_LOGD(TAG, "  VID:PID = 0x%04X:0x%04X", _USB_Manager_State.DeviceDescriptor.idVendor,
             _USB_Manager_State.DeviceDescriptor.idProduct);
    ESP_LOGD(TAG, "  Manufacturer: %s", _USB_Manager_State.StringDescriptors[1]);
    ESP_LOGD(TAG, "  Product: %s", _USB_Manager_State.StringDescriptors[2]);

    /* Set descriptors in TinyUSB config */
    USB_Config.descriptor.device = &_USB_Manager_State.DeviceDescriptor;
    USB_Config.descriptor.string = _USB_Manager_State.StringDescriptors;
    USB_Config.descriptor.string_count = 3;

    /* Set configuration descriptor based on mode */
    if (p_Config->Mode == USB_MODE_MSC) {
        USB_Config.descriptor.full_speed_config = get_Desc_Config_MSC();
        ESP_LOGD(TAG, "  Mode: MSC (Mass Storage Class)");
    } else if (p_Config->Mode == USB_MODE_UVC) {
        USB_Config.descriptor.full_speed_config = get_Desc_Config_UVC();
        ESP_LOGD(TAG, "  Mode: UVC (Video Class)");
    }

    ESP_LOGD(TAG, "Initializing TinyUSB...");
    Error = tinyusb_driver_install(&USB_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %d!", Error);

        return Error;
    }

    /* Initialize mode-specific module */
    if (p_Config->Mode == USB_MODE_MSC) {
        USB_MSC_Config_t MSC_Config = {
            .MountPoint = p_Config->MountPoint,
        };

        Error = USBMSC_Init(&MSC_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB MSC module: %d!", Error);
            return Error;
        }

        ESP_LOGI(TAG, "USB Mass Storage Device ready");
    } else if (p_Config->Mode == USB_MODE_UVC) {
        USB_UVC_Config_t UVC_Config = {
            .Width = 160,
            .Height = 120,
            .FrameRate = 9,
        };

        Error = USBUVC_Init(&UVC_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB UVC module: %d!", Error);
            return Error;
        }

        ESP_LOGI(TAG, "USB Video Class Device ready");
    }

    ESP_LOGI(TAG, "Connecting to USB bus...");
    if (tud_connect()) {
        ESP_LOGI(TAG, "USB connection established");
    } else {
        ESP_LOGW(TAG, "tud_connect() returned false (may already be connected)");
    }

    _USB_Manager_State.isInitialized = true;

    esp_event_post(USB_EVENTS, USB_EVENT_INITIALIZED, &_USB_Manager_State.Mode, sizeof(USB_Mode_t), portMAX_DELAY);

    /* Assume mounted after successful init */
    _USB_Manager_State.isUSBMounted = true;

    return ESP_OK;
}

esp_err_t USBManager_Deinit(void)
{
    esp_err_t Error;

    if (_USB_Manager_State.isInitialized == false) {
        ESP_LOGW(TAG, "USB Manager not initialized - nothing to deinit!");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting USB deinitialization (synchronous)...");

    _USB_Manager_State.isInitialized = false;

    ESP_LOGI(TAG, "Disconnecting from USB bus...");
    if (tud_disconnect()) {
        ESP_LOGI(TAG, "USB disconnect initiated successfully");
    } else {
        ESP_LOGW(TAG, "Failed to initiate USB disconnect");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    /* Deinitialize mode-specific module */
    if (_USB_Manager_State.Mode == USB_MODE_MSC) {
        Error = USBMSC_Deinit();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize USB MSC module: %d!", Error);
        } else {
            ESP_LOGD(TAG, "USB MSC module deinitialized");
        }
    } else if (_USB_Manager_State.Mode == USB_MODE_UVC) {
        Error = USBUVC_Deinit();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize USB UVC module: %d!", Error);
        } else {
            ESP_LOGD(TAG, "USB UVC module deinitialized");
        }
    }

    /* Uninstall TinyUSB driver to allow re-initialization with different descriptors */
    Error = tinyusb_driver_uninstall();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to uninstall TinyUSB driver: %d!", Error);
        _USB_Manager_State.isInitialized = true;
        return Error;
    }

    ESP_LOGD(TAG, "TinyUSB driver uninstalled");

    /* Allow TinyUSB stack to fully deinitialize before next init */
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_event_post(USB_EVENTS, USB_EVENT_UNINITIALIZED, NULL, 0, portMAX_DELAY);

    ESP_LOGI(TAG, "USB Manager deinitialized successfully");

    return ESP_OK;
}

bool USBManager_IsInitialized(void)
{
    return _USB_Manager_State.isInitialized;
}
