/*
 * memoryManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Memory management (Flash partitions and SD card).
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

#ifndef MEMORYMANAGER_H_
#define MEMORYMANAGER_H_

#include <esp_err.h>
#include <esp_event.h>
#include <wear_levelling.h>
#include <sdmmc_cmd.h>

#include <stdint.h>
#include <stdbool.h>

/** @brief Settings Manager events base.
 */
ESP_EVENT_DEFINE_BASE(MEMORY_EVENTS);

/** @brief Storage location types.
 */
typedef enum {
    MEMORY_LOCATION_INTERNAL,               /**< Internal flash storage partition. */
    MEMORY_LOCATION_SD_CARD,                /**< External SD card. */
} MemoryManager_Location_t;

/** @brief Memory usage information.
 */
typedef struct {
    size_t TotalBytes;                      /**< Total partition/storage size in bytes. */
    size_t UsedBytes;                       /**< Used space in bytes. */
    size_t FreeBytes;                       /**< Free space in bytes. */
    uint8_t UsedPercent;                    /**< Used space percentage (0-100). */
} MemoryManager_Usage_t;

/** @brief  Initialize the Memory Manager.
 *          Detects SD card presence and determines storage location.
 *  @return ESP_OK on success
 */
esp_err_t MemoryManager_Init(void);

/** @brief  Deinitialize the Memory Manager.
 *  @return ESP_OK on success
 */
esp_err_t MemoryManager_Deinit(void);

/** @brief  Check if SD card is present and mounted.
 *  @return true if SD card is available
 */
bool MemoryManager_HasSDCard(void);

/** @brief  Get current active storage location.
 *  @return Storage location (internal or SD card)
 */
MemoryManager_Location_t MemoryManager_GetStorageLocation(void);

/** @brief  Get base path for current storage location.
 *          Returns "/storage" for internal flash or "/sdcard" for SD card.
 *  @return Pointer to base path string
 */
const char *MemoryManager_GetStoragePath(void);

/** @brief          Get storage usage information for current location.
 *  @note           Storage must be mounted to get usage info.
 *  @param p_Usage  Pointer to usage structure to populate
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Usage is NULL
 *                  ESP_ERR_NOT_FOUND if storage not found
 *                  ESP_FAIL if filesystem not mounted
 */
esp_err_t MemoryManager_GetStorageUsage(MemoryManager_Usage_t *p_Usage);

/** @brief          Get coredump partition usage information.
 *  @note           Returns size of stored coredump data, if any exists.
 *  @param p_Usage  Pointer to usage structure to populate
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Usage is NULL
 *                  ESP_ERR_NOT_FOUND if coredump partition not found
 */
esp_err_t MemoryManager_GetCoredumpUsage(MemoryManager_Usage_t *p_Usage);

/** @brief          Lock filesystem for USB access.
 *                  Prevents application from accessing filesystem while USB is active.
 *                  Must be called before enabling USB Mass Storage.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if already locked
 */
esp_err_t MemoryManager_LockFilesystem(void);

/** @brief          Unlock filesystem for application access.
 *                  Allows application to access filesystem again after USB is deactivated.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not locked
 */
esp_err_t MemoryManager_UnlockFilesystem(void);

/** @brief          Check if filesystem is locked for USB access.
 *  @return         true if locked (USB active, app must not write)
 *                  false if unlocked (app can write)
 */
bool MemoryManager_IsFilesystemLocked(void);

/** @brief      Erase current storage location completely.
 *  @warning    This will delete all files in the active storage filesystem.
 *  @note       Filesystem will be automatically reformatted after erase.
 *  @return     ESP_OK on success
 *              ESP_ERR_NOT_FOUND if storage not found
 *              ESP_FAIL on erase failure
 */
esp_err_t MemoryManager_EraseStorage(void);

/** @brief      Erase coredump partition completely.
 *  @warning    This will delete any stored crash dumps.
 *  @note       Partition will be cleared and ready for new coredumps.
 *  @return     ESP_OK on success
 *              ESP_ERR_NOT_FOUND if coredump partition not found
 *              ESP_FAIL on erase failure
 */
esp_err_t MemoryManager_EraseCoredump(void);

/** @brief          Get wear leveling handle for internal storage.
 *  @note           Only valid for internal flash storage (not SD card).
 *  @param p_Handle Pointer to receive the wear leveling handle
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Handle is NULL
 *                  ESP_ERR_INVALID_STATE if not using internal storage or not mounted
 */
esp_err_t MemoryManager_GetWearLevelingHandle(wl_handle_t *p_Handle);

/** @brief          Get SD card handle.
 *  @note           Only valid when SD card storage is active.
 *  @param pp_Card  Pointer to receive the SD card handle pointer
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if pp_Card is NULL
 *                  ESP_ERR_INVALID_STATE if SD card not mounted
 */
esp_err_t MemoryManager_GetSDCardHandle(sdmmc_card_t **pp_Card);

#endif /* MEMORYMANAGER_H_ */
