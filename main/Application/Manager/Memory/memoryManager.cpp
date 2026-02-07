/*
 * memoryManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Memory management implementation (Flash partitions and SD card).
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
#include <esp_spiffs.h>
#include <esp_vfs_fat.h>

#include <driver/spi_master.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../Devices/SPI/spi.h"
#include "../Devices/devicesManager.h"
#include "memoryManager.h"

static const char *TAG = "MemoryManager";

typedef struct {
    bool isInitialized;
    bool hasSDCard;
    MemoryManager_Location_t StorageLocation;
    sdmmc_card_t *p_SDCard;
} MemoryManager_State_t;

static MemoryManager_State_t _State = {
    .isInitialized = false,
    .hasSDCard = false,
    .StorageLocation = MEMORY_LOCATION_INTERNAL,
    .p_SDCard = NULL
};

/** @brief          Calculate directory size recursively.
 *  @param p_Path   Path to directory
 *  @return         Total size in bytes
 */
static size_t calculate_dir_size(const char *p_Path)
{
    size_t TotalSize = 0;
    DIR *Dir = opendir(p_Path);

    if (Dir == NULL) {
        return 0;
    }

    struct dirent *Entry;
    while ((Entry = readdir(Dir)) != NULL) {
        if (strcmp(Entry->d_name, ".") == 0 || strcmp(Entry->d_name, "..") == 0) {
            continue;
        }

        char FullPath[256];
        snprintf(FullPath, sizeof(FullPath), "%s/%s", p_Path, Entry->d_name);

        struct stat St;
        if (stat(FullPath, &St) == 0) {
            if (S_ISDIR(St.st_mode)) {
                TotalSize += calculate_dir_size(FullPath);
            } else {
                TotalSize += St.st_size;
            }
        }
    }

    closedir(Dir);

    return TotalSize;
}

/** @brief  Try to mount SD card via SPI.
 *  @return ESP_OK if SD card mounted successfully
 */
static esp_err_t mount_sd_card(void)
{
    esp_err_t Error;
    spi_host_device_t SPI_Host = DevicesManager_GetSPIHost();

    ESP_LOGI(TAG, "Attempting to mount SD card via SPI...");

    if (SPIM_IsInitialized(SPI_Host) == false) {
        ESP_LOGE(TAG, "SPI bus not initialized! Call DevicesManager_Init() first.");

        return ESP_ERR_INVALID_STATE;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CS);
    slot_config.host_id = SPI_Host;

    /* FAT filesystem mount configuration */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_Host;

    Error = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &_State.p_SDCard);
    
    if (Error != ESP_OK) {
        if (Error == ESP_FAIL) {
            ESP_LOGW(TAG, "Failed to mount SD card filesystem");
        } else {
            ESP_LOGW(TAG, "Failed to initialize SD card: %d!", Error);
        }

        return Error;
    }

    ESP_LOGI(TAG, "SD card mounted successfully at /sdcard via SPI");

    return ESP_OK;
}

esp_err_t MemoryManager_Init(void)
{
    if (_State.isInitialized) {
        ESP_LOGW(TAG, "Memory Manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Memory Manager");

    /* Try to mount SD card first */
    if (mount_sd_card() == ESP_OK) {
        ESP_LOGI(TAG, "Using SD card for storage");

        _State.hasSDCard = true;
        _State.StorageLocation = MEMORY_LOCATION_SD_CARD;
    } else {
        ESP_LOGI(TAG, "Using internal flash for storage");

        _State.hasSDCard = false;
        _State.StorageLocation = MEMORY_LOCATION_INTERNAL;
    }

    _State.isInitialized = true;

    return ESP_OK;
}

esp_err_t MemoryManager_Deinit(void)
{
    if (_State.isInitialized == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing Memory Manager");

    if (_State.hasSDCard) {
        esp_vfs_fat_sdcard_unmount("/sdcard", _State.p_SDCard);
        _State.p_SDCard = NULL;
        _State.hasSDCard = false;
    }

    _State.isInitialized = false;

    return ESP_OK;
}

bool MemoryManager_HasSDCard(void)
{
    return _State.hasSDCard;
}

MemoryManager_Location_t MemoryManager_GetStorageLocation(void)
{
    return _State.StorageLocation;
}

const char *MemoryManager_GetStoragePath(void)
{
    if (_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
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

    if (_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        FATFS *fs;
        DWORD fre_clust;

        if (_State.hasSDCard == false) {
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
        /* Get internal storage usage */
        const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                            "storage");

        if (Partition == NULL) {
            ESP_LOGE(TAG, "Storage partition not found!");

            return ESP_ERR_NOT_FOUND;
        }

        p_Usage->TotalBytes = Partition->size;

        /* Try to get used space from SPIFFS */
        size_t TotalBytes = 0;
        size_t UsedBytes = 0;

        esp_err_t Error = esp_spiffs_info("storage", &TotalBytes, &UsedBytes);
        if (Error == ESP_OK) {
            p_Usage->UsedBytes = UsedBytes;
            p_Usage->FreeBytes = TotalBytes - UsedBytes;
            p_Usage->UsedPercent = (uint8_t)((UsedBytes * 100) / TotalBytes);
        } else {
            ESP_LOGW(TAG, "Could not get SPIFFS info (not mounted?), returning partition size only");
            p_Usage->UsedBytes = 0;
            p_Usage->FreeBytes = p_Usage->TotalBytes;
            p_Usage->UsedPercent = 0;
        }

        return ESP_OK;
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

    if (Error == ESP_OK && Magic != 0xFFFFFFFF && Magic != 0x00000000) {
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
    if (_State.StorageLocation == MEMORY_LOCATION_SD_CARD) {
        struct dirent *Entry;

        ESP_LOGI(TAG, "Erasing SD card storage...");

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

        ESP_LOGI(TAG, "SD card storage erased successfully");

        return ESP_OK;
        
    } else {
        /* Erase internal flash storage partition */
        const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                                                    "storage");

        if (Partition == NULL) {
            ESP_LOGE(TAG, "Storage partition not found!");

            return ESP_ERR_NOT_FOUND;
        }

        ESP_LOGI(TAG, "Erasing storage partition (%d bytes)...", Partition->size);

        esp_vfs_spiffs_unregister("storage");

        esp_err_t Error = esp_partition_erase_range(Partition, 0, Partition->size);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase storage partition: %d!", Error);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Storage partition erased successfully");

        /* Remount and format SPIFFS */
        esp_vfs_spiffs_conf_t Config = {
            .base_path = "/storage",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true
        };

        Error = esp_vfs_spiffs_register(&Config);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remount storage: %d!", Error);
        }

        return ESP_OK;
    }
}

esp_err_t MemoryManager_EraseCoredump(void)
{
    const esp_partition_t *Partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                                "coredump");

    if (Partition == NULL) {
        ESP_LOGE(TAG, "Coredump partition not found!");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Erasing coredump partition (%d bytes)...", Partition->size);

    esp_err_t Error = esp_partition_erase_range(Partition, 0, Partition->size);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase coredump partition: %d!", Error);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Coredump partition erased successfully");

    return ESP_OK;
}
