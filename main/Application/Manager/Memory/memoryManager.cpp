/*
 * memoryManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Memory Manager implementation.
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
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <wear_levelling.h>

#include <driver/spi_master.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../Devices/SPI/spi.h"
#include "../Devices/devicesManager.h"
#include "memoryManager.h"

ESP_EVENT_DEFINE_BASE(MEMORY_EVENTS);

typedef struct {
    bool isInitialized;
    bool hasSDCard;
    bool isFilesystemLocked;
    MemoryManager_Location_t StorageLocation;
    sdmmc_card_t *p_SDCard;
    wl_handle_t WL_Handle;
} Memory_Manager_State_t;

static Memory_Manager_State_t _Memory_Manager_State;

static const char *TAG = "Memory-Manager";

/** @brief          Calculate directory size recursively.
 *  @param p_Path   Path to directory
 *  @return         Total size in bytes
 */
static size_t MemoryManager_Calc_Dir_Size(const char *p_Path)
{
    size_t TotalSize = 0;
    DIR *Dir = opendir(p_Path);

    if (Dir == NULL) {
        return 0;
    }

    struct dirent *Entry;
    while ((Entry = readdir(Dir)) != NULL) {
        struct stat St;
        char FullPath[256];

        if ((strcmp(Entry->d_name, ".") == 0) || (strcmp(Entry->d_name, "..") == 0)) {
            continue;
        }

        snprintf(FullPath, sizeof(FullPath), "%s/%s", p_Path, Entry->d_name);

        if (stat(FullPath, &St) == 0) {
            if (S_ISDIR(St.st_mode)) {
                TotalSize += MemoryManager_Calc_Dir_Size(FullPath);
            } else {
                TotalSize += St.st_size;
            }
        }
    }

    closedir(Dir);

    return TotalSize;
}

/** @brief  Mount internal flash storage as FAT32 with wear leveling.
 *  @return ESP_OK if mounted successfully
 */
static esp_err_t MemoryManager_Mount_Internal_Storage(void)
{
    esp_err_t Error;
    const esp_vfs_fat_mount_config_t MountConfig = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false
    };
    const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_FAT,
                                                                "storage");

    ESP_LOGD(TAG, "Mounting internal storage with FAT32 and wear leveling...");

    if (Partition == NULL) {
        ESP_LOGE(TAG, "Storage partition not found!");

        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "Found storage partition: size=%lu bytes", Partition->size);

    Error = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &MountConfig, &_Memory_Manager_State.WL_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FAT filesystem: %d", Error);

        return Error;
    }

    ESP_LOGD(TAG, "Internal storage mounted successfully at /storage");
    ESP_LOGD(TAG, "Wear leveling handle: %d", _Memory_Manager_State.WL_Handle);

    return ESP_OK;
}

/** @brief  Try to mount SD card via SPI.
 *  @return ESP_OK if SD card mounted successfully
 */
static esp_err_t MemoryManager_Mount_SD_Card(void)
{
    esp_err_t Error;
    sdmmc_host_t Host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t SlotConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
    spi_host_device_t SPI_Host = DevicesManager_GetSPIHost();
    const esp_vfs_fat_mount_config_t MountConfig = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGD(TAG, "Attempting to mount SD card via SPI...");

    if (SPIM_IsInitialized(SPI_Host) == false) {
        ESP_LOGE(TAG, "SPI bus not initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    SlotConfig.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CS);
    SlotConfig.host_id = SPI_Host;
    Host.slot = SPI_Host;

    Error = esp_vfs_fat_sdspi_mount("/sdcard", &Host, &SlotConfig, &MountConfig, &_Memory_Manager_State.p_SDCard);
    if (Error != ESP_OK) {
        if (Error == ESP_FAIL) {
            ESP_LOGW(TAG, "Failed to mount SD card filesystem");
        } else {
            ESP_LOGW(TAG, "Failed to initialize SD card: %d!", Error);
        }

        return Error;
    }

    ESP_LOGD(TAG, "SD card mounted successfully at /sdcard via SPI");

    return ESP_OK;
}

esp_err_t MemoryManager_Init(void)
{
    esp_err_t Error;

    if (_Memory_Manager_State.isInitialized) {
        ESP_LOGW(TAG, "Memory Manager already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Memory Manager");

    memset(&_Memory_Manager_State, 0, sizeof(Memory_Manager_State_t));

    /* Try to mount SD card first */
    if (MemoryManager_Mount_SD_Card() == ESP_OK) {
        ESP_LOGD(TAG, "Using SD card for storage");

        _Memory_Manager_State.hasSDCard = true;
        _Memory_Manager_State.StorageLocation = MEMORY_LOCATION_SD_CARD;

        esp_event_post(MEMORY_EVENTS, MEMORY_EVENT_SD_CARD_MOUNTED, NULL, 0, portMAX_DELAY);
    } else {
        ESP_LOGD(TAG, "SD card not available, using internal flash");

        /* Mount internal flash with FAT32 */
        Error = MemoryManager_Mount_Internal_Storage();
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount internal storage: %d!", Error);

            return Error;
        }

        _Memory_Manager_State.hasSDCard = false;
        _Memory_Manager_State.StorageLocation = MEMORY_LOCATION_INTERNAL;
        esp_event_post(MEMORY_EVENTS, MEMORY_EVENT_FLASH_MOUNTED, NULL, 0, portMAX_DELAY);
    }

    _Memory_Manager_State.isInitialized = true;

    return ESP_OK;
}

esp_err_t MemoryManager_Deinit(void)
{
    if (_Memory_Manager_State.isInitialized == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Deinitializing Memory Manager");

    if (_Memory_Manager_State.hasSDCard) {
        esp_vfs_fat_sdcard_unmount("/sdcard", _Memory_Manager_State.p_SDCard);
        _Memory_Manager_State.p_SDCard = NULL;
        _Memory_Manager_State.hasSDCard = false;
    } else if (_Memory_Manager_State.WL_Handle != WL_INVALID_HANDLE) {
        esp_vfs_fat_spiflash_unmount_rw_wl("/storage", _Memory_Manager_State.WL_Handle);
        _Memory_Manager_State.WL_Handle = WL_INVALID_HANDLE;
    }

    _Memory_Manager_State.isInitialized = false;

    return ESP_OK;
}

bool MemoryManager_HasSDCard(void)
{
    return _Memory_Manager_State.hasSDCard;
}

MemoryManager_Location_t MemoryManager_GetStorageLocation(void)
{
    return _Memory_Manager_State.StorageLocation;
}

const char *MemoryManager_GetStoragePath(void)
{
    if (_Memory_Manager_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        return "/sdcard";
    } else {
        return "/storage";
    }
}

esp_err_t MemoryManager_GetStorageUsage(MemoryManager_Usage_t *p_Usage)
{
    if (p_Usage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_Memory_Manager_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        FATFS *fs;
        DWORD fre_clust;

        if (_Memory_Manager_State.hasSDCard == false) {
            ESP_LOGE(TAG, "SD card not mounted!");

            return ESP_ERR_INVALID_STATE;
        }

        if (f_getfree("0:", &fre_clust, &fs) == FR_OK) {
            uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
            uint64_t free_sectors = fre_clust * fs->csize;

            p_Usage->TotalBytes = total_sectors * fs->ssize;
            p_Usage->FreeBytes = free_sectors * fs->ssize;
            p_Usage->UsedBytes = p_Usage->TotalBytes - p_Usage->FreeBytes;

            if (p_Usage->TotalBytes > 0) {
                p_Usage->UsedPercent = (uint8_t)((p_Usage->UsedBytes * 100) / p_Usage->TotalBytes);
            } else {
                p_Usage->UsedPercent = 0;
            }

            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to get SD card filesystem info!");

            return ESP_FAIL;
        }
    } else {
        /* Get internal FAT storage usage */
        FATFS *fs;
        DWORD fre_clust;

        if (_Memory_Manager_State.WL_Handle == WL_INVALID_HANDLE) {
            ESP_LOGE(TAG, "Internal storage not mounted!");

            return ESP_ERR_INVALID_STATE;
        }

        if (f_getfree("1:", &fre_clust, &fs) == FR_OK) {
            uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
            uint64_t free_sectors = fre_clust * fs->csize;

            p_Usage->TotalBytes = total_sectors * fs->ssize;
            p_Usage->FreeBytes = free_sectors * fs->ssize;
            p_Usage->UsedBytes = p_Usage->TotalBytes - p_Usage->FreeBytes;

            if (p_Usage->TotalBytes > 0) {
                p_Usage->UsedPercent = (uint8_t)((p_Usage->UsedBytes * 100) / p_Usage->TotalBytes);
            } else {
                p_Usage->UsedPercent = 0;
            }

            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to get internal storage filesystem info!");

            return ESP_FAIL;
        }
    }
}

esp_err_t MemoryManager_GetCoredumpUsage(MemoryManager_Usage_t *p_Usage)
{
    if (p_Usage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                                "coredump");
    if (Partition == NULL) {
        ESP_LOGE(TAG, "Coredump partition not found!");

        return ESP_ERR_NOT_FOUND;
    }

    p_Usage->TotalBytes = Partition->size;

    /* Read first 4 bytes to check if coredump exists (magic number check) */
    uint32_t Magic = 0;
    esp_err_t Error = esp_partition_read(Partition, 0, &Magic, sizeof(Magic));

    if ((Error == ESP_OK) && (Magic != 0xFFFFFFFF) && (Magic != 0x00000000)) {
        /* Coredump likely present - assume partition is used */
        p_Usage->UsedBytes = p_Usage->TotalBytes;
        p_Usage->FreeBytes = 0;
        p_Usage->UsedPercent = 100;
    } else {
        /* No coredump or empty partition */
        p_Usage->UsedBytes = 0;
        p_Usage->FreeBytes = p_Usage->TotalBytes;
        p_Usage->UsedPercent = 0;
    }

    return ESP_OK;
}

esp_err_t MemoryManager_EraseStorage(void)
{
    if (_Memory_Manager_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        struct dirent *Entry;

        ESP_LOGD(TAG, "Erasing SD card storage...");

        DIR *Dir = opendir("/sdcard");
        if (Dir == NULL) {
            ESP_LOGE(TAG, "Failed to open SD card directory!");

            return ESP_FAIL;
        }

        while ((Entry = readdir(Dir)) != NULL) {
            char FilePath[512];

            if ((strcmp(Entry->d_name, ".") == 0) || (strcmp(Entry->d_name, "..") == 0)) {
                continue;
            }

            snprintf(FilePath, sizeof(FilePath), "/sdcard/%s", Entry->d_name);

            struct stat St;
            if (stat(FilePath, &St) == 0) {
                if (S_ISDIR(St.st_mode)) {
                    /* TODO: Implement recursive directory deletion */
                    ESP_LOGW(TAG, "Skipping directory: %s", FilePath);
                } else {
                    if (unlink(FilePath) != 0) {
                        ESP_LOGW(TAG, "Failed to delete: %s", FilePath);
                    }
                }
            }
        }

        closedir(Dir);

        ESP_LOGD(TAG, "SD card storage erased successfully");

        return ESP_OK;

    } else {
        esp_err_t Error;
        const esp_vfs_fat_mount_config_t MountConfig = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = 4096,
            .disk_status_check_enable = false
        };

        /* Erase internal FAT storage by unmounting and reformatting */
        ESP_LOGD(TAG, "Erasing internal storage...");

        /* Unmount current filesystem */
        if (_Memory_Manager_State.WL_Handle != WL_INVALID_HANDLE) {
            esp_vfs_fat_spiflash_unmount_rw_wl("/storage", _Memory_Manager_State.WL_Handle);
            _Memory_Manager_State.WL_Handle = WL_INVALID_HANDLE;
        }

        Error = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &MountConfig, &_Memory_Manager_State.WL_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remount storage: %d!", Error);

            return ESP_FAIL;
        }

        ESP_LOGD(TAG, "Internal storage erased and reformatted successfully");

        return ESP_OK;
    }
}

esp_err_t MemoryManager_EraseCoredump(void)
{
    esp_err_t Error;
    const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                                "coredump");

    if (Partition == NULL) {
        ESP_LOGE(TAG, "Coredump partition not found!");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "Erasing coredump partition (%d bytes)...", Partition->size);

    Error = esp_partition_erase_range(Partition, 0, Partition->size);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase coredump partition: %d!", Error);

        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Coredump partition erased successfully");

    return ESP_OK;
}

esp_err_t MemoryManager_GetWearLevelingHandle(wl_handle_t *p_Handle)
{
    if (p_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (_Memory_Manager_State.StorageLocation != MEMORY_LOCATION_INTERNAL) {
        ESP_LOGE(TAG, "Wear leveling only available for internal storage!");

        return ESP_ERR_INVALID_STATE;
    }

    if (_Memory_Manager_State.WL_Handle == WL_INVALID_HANDLE) {
        ESP_LOGE(TAG, "Internal storage not mounted!");

        return ESP_ERR_INVALID_STATE;
    }

    *p_Handle = _Memory_Manager_State.WL_Handle;

    return ESP_OK;
}

esp_err_t MemoryManager_GetSDCardHandle(sdmmc_card_t **pp_Card)
{
    if (pp_Card == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Memory_Manager_State.StorageLocation != MEMORY_LOCATION_SD_CARD) {
        ESP_LOGE(TAG, "SD card handle only available when SD card is mounted!");

        return ESP_ERR_INVALID_STATE;
    } else if (_Memory_Manager_State.p_SDCard == NULL) {
        ESP_LOGE(TAG, "SD card not mounted!");

        return ESP_ERR_INVALID_STATE;
    }

    *pp_Card = _Memory_Manager_State.p_SDCard;

    return ESP_OK;
}

esp_err_t MemoryManager_LockFilesystem(void)
{
    if (_Memory_Manager_State.isFilesystemLocked) {
        ESP_LOGW(TAG, "Filesystem already locked!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Locking filesystem for USB access");
    ESP_LOGW(TAG, "  Application MUST NOT write to %s while USB is active!", MemoryManager_GetStoragePath());

    _Memory_Manager_State.isFilesystemLocked = true;

    return ESP_OK;
}

esp_err_t MemoryManager_UnlockFilesystem(void)
{
    if (_Memory_Manager_State.isFilesystemLocked == false) {
        ESP_LOGW(TAG, "Filesystem not locked!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Unlocking filesystem - application can write again");

    _Memory_Manager_State.isFilesystemLocked = false;

    return ESP_OK;
}

bool MemoryManager_IsFilesystemLocked(void)
{
    return _Memory_Manager_State.isFilesystemLocked;
}

esp_err_t MemoryManager_FormatActiveStorage(void)
{
    esp_err_t Error;

    if (_Memory_Manager_State.isFilesystemLocked == false) {
        ESP_LOGW(TAG, "Filesystem not locked! Lock filesystem before formatting.");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Formatting active storage location...");

    if (_Memory_Manager_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        sdmmc_host_t Host = SDSPI_HOST_DEFAULT();
        sdspi_device_config_t SlotConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
        spi_host_device_t SPI_Host = DevicesManager_GetSPIHost();
        const esp_vfs_fat_mount_config_t MountConfig = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
        };

        ESP_LOGD(TAG, "Formatting SD card...");

        /* Unmount current SD card */
        if (_Memory_Manager_State.hasSDCard) {
            esp_vfs_fat_sdcard_unmount("/sdcard", _Memory_Manager_State.p_SDCard);
            _Memory_Manager_State.p_SDCard = NULL;
        }

        /* Format by creating new filesystem */
        SlotConfig.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CS);
        SlotConfig.host_id = SPI_Host;
        Host.slot = SPI_Host;

        Error = esp_vfs_fat_sdspi_mount("/sdcard", &Host, &SlotConfig, &MountConfig, &_Memory_Manager_State.p_SDCard);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to format and remount SD card: %d!", Error);
            _Memory_Manager_State.hasSDCard = false;

            return Error;
        }

        ESP_LOGI(TAG, "SD card formatted and remounted successfully");
        _Memory_Manager_State.hasSDCard = true;

        return ESP_OK;

    } else {
        const esp_vfs_fat_mount_config_t MountConfig = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = 4096,
            .disk_status_check_enable = false
        };

        ESP_LOGD(TAG, "Formatting internal storage...");

        /* Unmount current filesystem */
        if (_Memory_Manager_State.WL_Handle != WL_INVALID_HANDLE) {
            esp_vfs_fat_spiflash_unmount_rw_wl("/storage", _Memory_Manager_State.WL_Handle);
            _Memory_Manager_State.WL_Handle = WL_INVALID_HANDLE;
        }

        /* Format and remount */
        Error = esp_vfs_fat_spiflash_format_rw_wl("/storage", "storage");
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to format internal storage: %d!", Error);

            return Error;
        }

        Error = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &MountConfig, &_Memory_Manager_State.WL_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remount internal storage after formatting: %d!", Error);

            return Error;
        }

        ESP_LOGI(TAG, "Internal storage formatted and remounted successfully");

        return ESP_OK;
    }
}