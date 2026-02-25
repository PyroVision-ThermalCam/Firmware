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

#include <tinyusb.h>
#include <tinyusb_default_config.h>

#include <string.h>

#include "usbManager.h"
#include "MSC/usbMSC.h"
#include "UVC/usbUVC.h"
#include "CDC/usbCDC.h"
#include "Descriptors/descriptors.h"

ESP_EVENT_DEFINE_BASE(USB_EVENTS);

/** @brief USB Manager internal state.
 */
typedef struct {
    bool isInitialized;                     /**< Initialization state. */
    bool MSC_Enabled;                       /**< MSC class is active. */
    bool UVC_Enabled;                       /**< UVC class is active. */
    bool CDC_Enabled;                       /**< CDC class is active. */
    const char *StringDescriptors[4];       /**< String descriptor pointers (LangID, Manufacturer, Product, Serial). */
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

    if (p_Config->MSC_Enabled && (p_Config->MountPoint == NULL)) {
        ESP_LOGE(TAG, "MSC requires a valid MountPoint!");

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing USB Manager (composite mode: MSC=%d UVC=%d CDC=%d)...",
             p_Config->MSC_Enabled, p_Config->UVC_Enabled, p_Config->CDC_Enabled);

    memset(&_USB_Manager_State, 0, sizeof(USB_Manager_State_t));

    _USB_Manager_State.MSC_Enabled = p_Config->MSC_Enabled;
    _USB_Manager_State.UVC_Enabled = p_Config->UVC_Enabled;
    _USB_Manager_State.CDC_Enabled = p_Config->CDC_Enabled;

    /* String descriptors: 0=LangID, 1=Manufacturer, 2=Product, 3=Serial */
    _USB_Manager_State.StringDescriptors[0] = USB_LangID;
    _USB_Manager_State.StringDescriptors[1] = CONFIG_DEVICE_MANUFACTURER;
    _USB_Manager_State.StringDescriptors[2] = CONFIG_DEVICE_NAME;
    _USB_Manager_State.StringDescriptors[3] = NULL;

    ESP_LOGD(TAG, "USB Descriptors: VID:PID = 0x%04X:0x%04X",
             get_Desc_Device()->idVendor,
             get_Desc_Device()->idProduct);
    ESP_LOGD(TAG, "  Manufacturer: %s, Product: %s",
             _USB_Manager_State.StringDescriptors[1], _USB_Manager_State.StringDescriptors[2]);

    USB_Config.descriptor.device = get_Desc_Device();
    USB_Config.descriptor.full_speed_config = get_Desc_Config();
    USB_Config.descriptor.string = _USB_Manager_State.StringDescriptors;
    USB_Config.descriptor.string_count = 3;

    ESP_LOGD(TAG, "Installing TinyUSB driver...");
    Error = tinyusb_driver_install(&USB_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %d!", Error);

        return Error;
    }

    /* Initialize MSC class */
    if (_USB_Manager_State.MSC_Enabled) {
#if(defined CONFIG_TINYUSB_MSC_ENABLED) && (CONFIG_TINYUSB_MSC_ENABLED == 1)
        USB_MSC_Config_t MSC_Config = {
            .MountPoint = p_Config->MountPoint,
        };

        Error = USBMSC_Init(&MSC_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB MSC: %d!", Error);
            tinyusb_driver_uninstall();

            return Error;
        }

        ESP_LOGI(TAG, "USB Mass Storage Device ready");
#else
        ESP_LOGW(TAG, "MSC requested but CONFIG_TINYUSB_MSC_ENABLED is not set - skipping.");
        _USB_Manager_State.MSC_Enabled = false;
#endif
    }

    if (_USB_Manager_State.UVC_Enabled) {
#ifdef CONFIG_TINYUSB_UVC_ENABLED
        USB_UVC_Config_t UVC_Config = {
            .Width = 160,
            .Height = 120,
            .FrameRate = 9,
        };

        Error = USBUVC_Init(&UVC_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB UVC: %d!", Error);

            if (_USB_Manager_State.MSC_Enabled) {
                USBMSC_Deinit();
            }

            tinyusb_driver_uninstall();

            return Error;
        }

        ESP_LOGI(TAG, "USB Video Class Device ready");
#else
        ESP_LOGW(TAG, "UVC requested but CONFIG_TINYUSB_UVC_ENABLED is not set - skipping.");
        _USB_Manager_State.UVC_Enabled = false;
#endif
    }

    if (_USB_Manager_State.CDC_Enabled) {
#ifdef CONFIG_TINYUSB_CDC_ENABLED
        USB_CDC_Config_t CDC_Config = {
            .Reserved = 0,
        };

        Error = USBCDC_Init(&CDC_Config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize USB CDC: %d!", Error);

            if (_USB_Manager_State.UVC_Enabled) {
                USBUVC_Deinit();
            }

            if (_USB_Manager_State.MSC_Enabled) {
                USBMSC_Deinit();
            }

            tinyusb_driver_uninstall();

            return Error;
        }

        ESP_LOGI(TAG, "USB CDC-ACM Device ready");
#else
        ESP_LOGW(TAG, "CDC requested but CONFIG_TINYUSB_CDC_ENABLED is not set - skipping.");
        _USB_Manager_State.CDC_Enabled = false;
#endif
    }

    ESP_LOGI(TAG, "Connecting to USB bus...");
    if (tud_connect() == false) {
        ESP_LOGW(TAG, "tud_connect() returned false (may already be connected)");
    }

    _USB_Manager_State.isInitialized = true;

    esp_event_post(USB_EVENTS, USB_EVENT_INITIALIZED, NULL, 0, portMAX_DELAY);

    ESP_LOGI(TAG, "USB Manager initialized successfully");

    return ESP_OK;
}

esp_err_t USBManager_Deinit(void)
{
    esp_err_t Error;

    if (_USB_Manager_State.isInitialized == false) {
        ESP_LOGW(TAG, "USB Manager not initialized - nothing to deinit!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing USB Manager...");

    _USB_Manager_State.isInitialized = false;

    ESP_LOGD(TAG, "Disconnecting from USB bus...");
    if (tud_disconnect() == false) {
        ESP_LOGW(TAG, "Failed to initiate USB disconnect");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    /* Deinitialize CDC */
    if (_USB_Manager_State.CDC_Enabled) {
        Error = USBCDC_Deinit();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize USB CDC: %d!", Error);
        } else {
            ESP_LOGD(TAG, "USB CDC deinitialized");
        }
    }

    /* Deinitialize UVC */
    if (_USB_Manager_State.UVC_Enabled) {
        Error = USBUVC_Deinit();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize USB UVC: %d!", Error);
        } else {
            ESP_LOGD(TAG, "USB UVC deinitialized");
        }
    }

    /* Deinitialize MSC */
    if (_USB_Manager_State.MSC_Enabled) {
        Error = USBMSC_Deinit();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deinitialize USB MSC: %d!", Error);
        } else {
            ESP_LOGD(TAG, "USB MSC deinitialized");
        }
    }

    /* Uninstall TinyUSB driver */
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

bool USBManager_IsUVCEnabled(void)
{
    return _USB_Manager_State.isInitialized && _USB_Manager_State.UVC_Enabled;
}

bool USBManager_IsCDCEnabled(void)
{
    return _USB_Manager_State.isInitialized && _USB_Manager_State.CDC_Enabled;
}

bool USBManager_IsMSCEnabled(void)
{
    return _USB_Manager_State.isInitialized && _USB_Manager_State.MSC_Enabled;
}
