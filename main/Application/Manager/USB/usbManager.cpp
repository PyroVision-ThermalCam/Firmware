/*
 * usbManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Manager implementation.
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
#include <tusb.h>
#include <class/msc/msc_device.h>
#include <tinyusb_default_config.h>
#include <tinyusb_msc.h>
#include <wear_levelling.h>

#include <string.h>
#include <sys/stat.h>

#include "usbManager.h"
#include "descriptors.h"
#include "../Memory/memoryManager.h"

ESP_EVENT_DEFINE_BASE(USB_EVENTS);

static const char *TAG = "USB-Manager";

/** @brief USB Manager internal state.
 */
typedef struct {
    bool isInitialized;                     /**< Initialization state. */
    bool isDeinitializing;                  /**< Deinitialization in progress. */
    bool isUSBMounted;                      /**< USB host has mounted device. */
    USB_Manager_Config_t Config;            /**< Current configuration. */
    tinyusb_msc_storage_handle_t Storage;   /**< Storage handle. */
    TaskHandle_t DeinitTask;                /**< Deinit task handle. */
    tusb_desc_device_t DeviceDescriptor;    /**< Custom device descriptor. */
    const char *StringDescriptors[4];       /**< String descriptor pointers. */
} USB_Manager_State_t;

static USB_Manager_State_t _USB_Manager_State;

esp_err_t USBManager_Init(const USB_Manager_Config_t *p_Config)
{
    esp_err_t Error;

    if (p_Config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration pointer!");

        return ESP_ERR_INVALID_ARG;
    } else if (_USB_Manager_State.isInitialized) {
        ESP_LOGW(TAG, "USB Manager already initialized!");

        return ESP_ERR_INVALID_STATE;
    } else if (p_Config->MountPoint == NULL) {
        ESP_LOGE(TAG, "Mount point is NULL!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Wait for any pending deinitialization to complete */
    if (_USB_Manager_State.isDeinitializing) {
        ESP_LOGD(TAG, "Waiting for previous deinit to complete...");

        uint8_t WaitCount = 0;
        while ((_USB_Manager_State.isDeinitializing) && (WaitCount < 100)) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            WaitCount++;
        }

        if (_USB_Manager_State.isDeinitializing) {
            ESP_LOGE(TAG, "Previous deinit still running - cannot initialize!");

            return ESP_ERR_INVALID_STATE;
        }

        ESP_LOGD(TAG, "Previous deinit completed, proceeding with init");

        /* Additional delay to ensure cleanup is complete */
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    /* Detect active storage type from MemoryManager */
    MemoryManager_Location_t StorageLocation = MemoryManager_GetStorageLocation();
    const char *p_StorageTypeName = (StorageLocation == MEMORY_LOCATION_SD_CARD) ? "SD Card (FAT32)" :
                                    "Internal Flash (FAT32)";

    ESP_LOGD(TAG, "Initializing USB Manager...");

    memset(&_USB_Manager_State, 0, sizeof(USB_Manager_State_t));

    ESP_LOGD(TAG, "  Storage type: %s (auto-detected)", p_StorageTypeName);
    ESP_LOGD(TAG, "  Mount point: %s", p_Config->MountPoint);

    /* Lock filesystem for USB access */
    Error = MemoryManager_LockFilesystem();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to lock filesystem: %d!", Error);
    }

    memcpy(&_USB_Manager_State.Config, p_Config, sizeof(USB_Manager_Config_t));

    /* Configure the Device Descriptor */
    memcpy(&_USB_Manager_State.DeviceDescriptor, get_Desc_Device(), sizeof(tusb_desc_device_t));

    /* Configure the String Descriptors */
    _USB_Manager_State.StringDescriptors[0] = NULL;                                 // 0: Language (set by TinyUSB)
    _USB_Manager_State.StringDescriptors[1] = p_Config->Manufacturer;               // 1: Manufacturer
    _USB_Manager_State.StringDescriptors[2] = p_Config->Product;                    // 2: Product
    _USB_Manager_State.StringDescriptors[3] = p_Config->SerialNumber;               // 3: Serial Number

    ESP_LOGD(TAG, "USB Descriptors:");
    ESP_LOGD(TAG, "  VID:PID = 0x%04X:0x%04X", _USB_Manager_State.DeviceDescriptor.idVendor,
             _USB_Manager_State.DeviceDescriptor.idProduct);
    ESP_LOGD(TAG, "  Manufacturer: %s", p_Config->Manufacturer ? p_Config->Manufacturer : "(null)");
    ESP_LOGD(TAG, "  Product: %s", p_Config->Product ? p_Config->Product : "(null)");
    ESP_LOGD(TAG, "  Serial: %s", p_Config->SerialNumber ? p_Config->SerialNumber : "(null)");

    ESP_LOGD(TAG, "Initializing TinyUSB...");

    tinyusb_config_t USB_Config = TINYUSB_DEFAULT_CONFIG();

    /* Override with custom descriptors */
    USB_Config.descriptor.device = &_USB_Manager_State.DeviceDescriptor;
    USB_Config.descriptor.string = _USB_Manager_State.StringDescriptors;
    USB_Config.descriptor.string_count = 4;

    Error = tinyusb_driver_install(&USB_Config);
    if (Error != ESP_OK) {
        if (Error == ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "TinyUSB driver already installed, reusing...");
        } else {
            ESP_LOGE(TAG, "Failed to install TinyUSB driver: %d!", Error);

            return Error;
        }
    }

    /* Clean up old storage handle if it still exists from previous session */
    if (_USB_Manager_State.Storage != NULL) {
        ESP_LOGD(TAG, "Cleaning up old storage handle from previous session...");

        Error = tinyusb_msc_delete_storage(_USB_Manager_State.Storage);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete old storage: %d (continuing anyway)!", Error);
        }

        _USB_Manager_State.Storage = NULL;

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    /* Initialize MSC storage based on detected storage location */
    if (StorageLocation == MEMORY_LOCATION_SD_CARD) {
        sdmmc_card_t *p_Card = NULL;

        /* Configure SD card MSC */
        ESP_LOGD(TAG, "Configuring SD Card MSC...");

        /* Get SD card handle from MemoryManager */
        Error = MemoryManager_GetSDCardHandle(&p_Card);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SD card handle: %d!", Error);

            return Error;
        }

        ESP_LOGD(TAG, "Using SD card from MemoryManager");

        tinyusb_msc_storage_config_t Storage_Config;
        memset(&Storage_Config, 0, sizeof(tinyusb_msc_storage_config_t));
        Storage_Config.medium.card = p_Card;

        Error = tinyusb_msc_new_storage_sdmmc(&Storage_Config, &_USB_Manager_State.Storage);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SD card storage: %d!", Error);

            return Error;
        }
    } else if (StorageLocation == MEMORY_LOCATION_INTERNAL) {
        wl_handle_t WL_Handle;

        /* Configure SPI flash (FAT32 with Wear Leveling) MSC */
        ESP_LOGD(TAG, "Configuring internal flash MSC...");

        /* Get existing wear leveling handle from MemoryManager */
        Error = MemoryManager_GetWearLevelingHandle(&WL_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get wear leveling handle: %d!", Error);

            return Error;
        }

        ESP_LOGD(TAG, "Using existing wear leveling handle: %d", WL_Handle);

        tinyusb_msc_storage_config_t Storage_Config;
        memset(&Storage_Config, 0, sizeof(tinyusb_msc_storage_config_t));
        Storage_Config.medium.wl_handle = WL_Handle;

        Error = tinyusb_msc_new_storage_spiflash(&Storage_Config, &_USB_Manager_State.Storage);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create internal flash storage: %d!", Error);

            return Error;
        }
    } else {
        ESP_LOGE(TAG, "Unknown storage location: %d!", StorageLocation);

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "USB Mass Storage Device ready");
    ESP_LOGD(TAG, "   WARNING: Do not access filesystem from application while USB is connected!");

    ESP_LOGI(TAG, "Connecting to USB bus...");
    if (tud_connect()) {
        ESP_LOGI(TAG, "USB connection established");
    } else {
        ESP_LOGW(TAG, "tud_connect() returned false (may already be connected)");
    }

    _USB_Manager_State.isInitialized = true;

    esp_event_post(USB_EVENTS, USB_EVENT_INITIALIZED, NULL, 0, portMAX_DELAY);

    /* Assume mounted after successful init */
    _USB_Manager_State.isUSBMounted = true;

    return ESP_OK;
}

/** @brief          Background task for safe USB deinitialization.
 *  @note           This task runs the deinitialization sequence asynchronously
 *                  to avoid blocking the GUI task during the lengthy USB shutdown.
 *                  Storage is NOT deleted to prevent crashes from pending USB callbacks.
 *  @param p_Param  Task parameters (unused)
 */
static void Task_USB_Deinit(void *p_Param)
{
    esp_err_t Error;

    ESP_LOGD(TAG, "USB deinit task started");

    ESP_LOGI(TAG, "Disconnecting from USB bus...");
    if (tud_disconnect()) {
        ESP_LOGI(TAG, "USB disconnect initiated successfully");
    } else {
        ESP_LOGW(TAG, "Failed to initiate USB disconnect");
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_LOGD(TAG, "Storage kept alive to prevent callback crashes");

    Error = MemoryManager_UnlockFilesystem();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unlock filesystem: %d!", Error);
    } else {
        ESP_LOGD(TAG, "Filesystem unlocked for application");
    }

    _USB_Manager_State.isDeinitializing = false;
    _USB_Manager_State.DeinitTask = NULL;

    esp_event_post(USB_EVENTS, USB_EVENT_UNINITIALIZED, NULL, 0, portMAX_DELAY);

    ESP_LOGD(TAG, "USB Manager deinitialized successfully");

    vTaskDelete(NULL);
}

esp_err_t USBManager_Deinit(void)
{
    if (_USB_Manager_State.isInitialized == false) {
        ESP_LOGW(TAG, "USB Manager not initialized!");

        return ESP_ERR_INVALID_STATE;
    } else if (_USB_Manager_State.isDeinitializing) {
        ESP_LOGW(TAG, "USB Manager is already deinitializing!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Starting USB deinitialization...");

    _USB_Manager_State.isInitialized = false;
    _USB_Manager_State.isDeinitializing = true;

    /* Create background task to handle the lengthy USB shutdown process */
    BaseType_t Result = xTaskCreate(
                            Task_USB_Deinit,
                            "Task_USB_Deinit",
                            4096,
                            NULL,
                            1,
                            &_USB_Manager_State.DeinitTask
                        );

    if (Result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB deinit task!");

        _USB_Manager_State.isDeinitializing = false;
        _USB_Manager_State.isInitialized = true;

        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "USB deinitialization task started (async)");

    return ESP_OK;
}

bool USBManager_IsInitialized(void)
{
    return _USB_Manager_State.isInitialized;
}
