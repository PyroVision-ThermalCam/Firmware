/*
 * usbMSC.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Mass Storage Class module implementation.
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

#include <sdkconfig.h>

#include "usbMSC.h"

#include <tinyusb.h>
#include <tinyusb_msc.h>
#include <wear_levelling.h>

#include <string.h>

#include "../../Memory/memoryManager.h"

/** @brief USB MSC internal state.
 */
typedef struct {
    bool IsInitialized;                     /**< Initialization state. */
    tinyusb_msc_storage_handle_t Storage;   /**< Storage handle. */
} USBMSC_State_t;

static USBMSC_State_t _MSCState;

static const char *TAG = "USB-MSC";

esp_err_t USBMSC_Init(const USB_MSC_Config_t *p_Config)
{
    esp_err_t Error;
    MemoryManager_Location_t StorageLocation;

    if (p_Config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration pointer!");

        return ESP_ERR_INVALID_ARG;
    } else if (_MSCState.IsInitialized) {
        ESP_LOGW(TAG, "USB MSC already initialized!");

        return ESP_ERR_INVALID_STATE;
    } else if (p_Config->MountPoint == NULL) {
        ESP_LOGE(TAG, "Mount point is NULL!");

        return ESP_ERR_INVALID_ARG;
    }

    memset(&_MSCState, 0, sizeof(USBMSC_State_t));

    /* Detect active storage type from MemoryManager */
    StorageLocation = MemoryManager_GetStorageLocation();
    const char *p_StorageTypeName = (StorageLocation == MEMORY_LOCATION_SD_CARD) ? "SD Card (FAT32)" :
                                    "Internal Flash (FAT32)";

    ESP_LOGD(TAG, "Initializing USB MSC...");
    ESP_LOGD(TAG, "  Storage type: %s (auto-detected)", p_StorageTypeName);
    ESP_LOGD(TAG, "  Mount point: %s", p_Config->MountPoint);

    /* Lock filesystem to prevent application writes during MSC operation */
    Error = MemoryManager_LockFilesystem();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to lock filesystem: 0x%X!", Error);
    }

    /* Soft-unmount VFS so USB host gets exclusive block-level access.
       The underlying storage handles (WL handle or sdmmc_card) are preserved. */
    Error = MemoryManager_SoftUnmountStorage();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to soft-unmount storage for MSC: 0x%X!", Error);

        MemoryManager_UnlockFilesystem();

        return Error;
    }

    if (StorageLocation == MEMORY_LOCATION_SD_CARD) {
        sdmmc_card_t *Card;
        tinyusb_msc_storage_config_t Storage_Config;

        ESP_LOGD(TAG, "Configuring SD Card MSC...");

        Error = MemoryManager_GetSDCardHandle(&Card);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SD card handle: 0x%X!", Error);

            MemoryManager_SoftRemountStorage();
            MemoryManager_UnlockFilesystem();

            return Error;
        }

        ESP_LOGD(TAG, "Using SD card from MemoryManager");

        memset(&Storage_Config, 0, sizeof(tinyusb_msc_storage_config_t));
        Storage_Config.medium.card = Card;
        Storage_Config.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;

        Error = tinyusb_msc_new_storage_sdmmc(&Storage_Config, &_MSCState.Storage);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SD card MSC storage: 0x%X!", Error);

            MemoryManager_SoftRemountStorage();
            MemoryManager_UnlockFilesystem();

            return Error;
        }
    } else if (StorageLocation == MEMORY_LOCATION_INTERNAL) {
        wl_handle_t WL_Handle;
        tinyusb_msc_storage_config_t Storage_Config;

        ESP_LOGD(TAG, "Configuring internal flash MSC...");

        Error = MemoryManager_GetWearLevelingHandle(&WL_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get wear leveling handle: 0x%X!", Error);

            MemoryManager_SoftRemountStorage();
            MemoryManager_UnlockFilesystem();

            return Error;
        }

        ESP_LOGD(TAG, "Using existing wear leveling handle: 0x%X", WL_Handle);

        memset(&Storage_Config, 0, sizeof(tinyusb_msc_storage_config_t));
        Storage_Config.medium.wl_handle = WL_Handle;
        Storage_Config.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;

        Error = tinyusb_msc_new_storage_spiflash(&Storage_Config, &_MSCState.Storage);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create internal flash MSC storage: 0x%X!", Error);

            MemoryManager_SoftRemountStorage();
            MemoryManager_UnlockFilesystem();

            return Error;
        }
    } else {
        ESP_LOGE(TAG, "Unknown storage location: 0x%X!", StorageLocation);

        MemoryManager_SoftRemountStorage();
        MemoryManager_UnlockFilesystem();

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "USB Mass Storage Device ready");
    ESP_LOGD(TAG, "   WARNING: Do not access filesystem from application while USB is connected!");

    _MSCState.IsInitialized = true;

    return ESP_OK;
}

esp_err_t USBMSC_Deinit(void)
{
    esp_err_t Error;

    if (_MSCState.IsInitialized == false) {
        ESP_LOGW(TAG, "USB MSC not initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Deinitializing USB MSC...");

    if (_MSCState.Storage != NULL) {
        Error = tinyusb_msc_delete_storage(_MSCState.Storage);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete MSC storage: 0x%X!", Error);
        } else {
            ESP_LOGD(TAG, "MSC storage deleted successfully");
        }

        _MSCState.Storage = NULL;
    }

    Error = MemoryManager_SoftRemountStorage();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to soft-remount storage after MSC: 0x%X!", Error);
    } else {
        ESP_LOGD(TAG, "Filesystem remounted for application");
    }

    Error = MemoryManager_UnlockFilesystem();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unlock filesystem: 0x%X!", Error);
    } else {
        ESP_LOGD(TAG, "Filesystem unlocked for application");
    }

    _MSCState.IsInitialized = false;

    ESP_LOGD(TAG, "USB MSC deinitialized successfully");

    return ESP_OK;
}

bool USBMSC_IsInitialized(void)
{
    return _MSCState.IsInitialized;
}
