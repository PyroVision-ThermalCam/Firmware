/*
 * memoryManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Memory Manager definition.
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

#include <wear_levelling.h>
#include <sdmmc_cmd.h>

#include "memoryTypes.h"

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

/** @brief          Initialize the Memory Manager.
 *                  Detects SD card presence and determines storage location (internal flash
 *                  or SD card). Mounts the filesystem and prepares it for use.
 *  @note           SD card is preferred if detected, otherwise internal flash is used.
 *                  Internal storage uses LittleFS with wear leveling.
 *                  SD card uses FAT filesystem.
 *  @warning        Must be called after SPI bus initialization (DevicesManager).
 *  @return         ESP_OK on success
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_FAIL if filesystem mount fails
 */
esp_err_t MemoryManager_Init(void);

/** @brief          Deinitialize the Memory Manager.
 *                  Unmounts filesystem, releases SD card handle, and frees all
 *                  allocated resources.
 *  @note           All open file handles must be closed before calling this.
 *  @warning        Storage path and handles become invalid after this call.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if unmount fails
 */
esp_err_t MemoryManager_Deinit(void);

/** @brief          Check if SD card is present and mounted.
 *                  Returns the SD card availability status. True means SD card is
 *                  currently in use as active storage.
 *  @note           SD card is hot-plug capable but requires re-initialization.
 *                  This is thread-safe.
 *  @return         true if SD card is detected and mounted
 *                  false if using internal flash or SD not detected
 */
bool MemoryManager_HasSDCard(void);

/** @brief          Get current active storage location.
 *                  Returns whether internal flash or SD card is currently used for
 *                  file storage operations.
 *  @note           Location is determined at initialization time.
 *                  This is thread-safe.
 *  @return         MEMORY_LOCATION_INTERNAL for internal flash
 *                  MEMORY_LOCATION_SD_CARD for SD card
 */
MemoryManager_Location_t MemoryManager_GetStorageLocation(void);

/** @brief          Get base path for current storage location.
 *                  Returns the mount point path for file operations.
 *                  Use this path as base for all file access.
 *  @note           Path is valid until MemoryManager_Deinit() is called.
 *                  NULL if not initialized
 *                  Path is constant and can be cached.
 *                  Append filename with e.g. "/storage/image.png".
 *                  Always prepend this path to filename for file operations.
 *  @return         "/storage" for internal flash
 *                  "/sdcard" for SD card
 */
const char *MemoryManager_GetStoragePath(void);

/** @brief          Get storage usage information for current location.
 *                  Retrieves total, used, and free space in bytes, plus usage percentage
 *                  for the active storage location.
 *  @note           Storage must be mounted to get usage information.
 *                  Values are approximate for wear-leveled storage.
 *  @param p_Usage  Pointer to MemoryManager_Usage_t structure to populate
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Usage is NULL
 *                  ESP_ERR_NOT_FOUND if storage not found
 *                  ESP_FAIL if filesystem not mounted
 */
esp_err_t MemoryManager_GetStorageUsage(MemoryManager_Usage_t *p_Usage);

/** @brief          Get coredump partition usage information.
 *                  Returns size of stored crash dump data if any exists in the
 *                  dedicated coredump partition.
 *  @note           Coredump partition is separate from application storage.
 *                  Use MemoryManager_EraseCoredump() to clear crash dumps.
 *  @param p_Usage  Pointer to MemoryManager_Usage_t structure to populate
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Usage is NULL
 *                  ESP_ERR_NOT_FOUND if coredump partition not found
 */
esp_err_t MemoryManager_GetCoredumpUsage(MemoryManager_Usage_t *p_Usage);

/** @brief          Lock filesystem for USB Mass Storage access.
 *                  Prevents application from accessing filesystem while USB MSC is active.
 *                  Must be called before enabling USB Mass Storage to prevent corruption.
 *  @note           All application file operations will fail when locked.
 *                  GUI should show "USB Mode Active" warning.
 *  @warning        Do NOT write to filesystem while locked - causes corruption!
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if already locked
 */
esp_err_t MemoryManager_LockFilesystem(void);

/** @brief          Unlock filesystem for application access.
 *                  Allows application to access filesystem again after USB Mass Storage
 *                  is deactivated.
 *  @note           Call this after USB MSC is disconnected.
 *                  Application can resume normal file operations.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not locked
 */
esp_err_t MemoryManager_UnlockFilesystem(void);

/** @brief          Check if filesystem is locked for USB access.
 *                  Returns current filesystem lock status. Use this to check before
 *                  any file write operations.
 *  @note           This is thread-safe and fast - check before every write.
 *                  Image saving, logging, and config writes should check this.
 *  @return         true if locked (USB active, app must not write)
 *                  false if unlocked (app can write normally)
 */
bool MemoryManager_IsFilesystemLocked(void);

/** @brief          Soft-unmount the active storage VFS filesystem.
 *                  Unmounts the FAT filesystem and unregisters the VFS path, but preserves
 *                  the underlying storage handles (wear leveling handle or SD card handle).
 *                  This allows USB Mass Storage to access the raw storage blocks while
 *                  the VFS path is no longer accessible by the application.
 *  @note           Must be called before USB MSC exposes the storage to the host.
 *                  The storage handles remain valid and can be used by TinyUSB MSC.
 *                  Call MemoryManager_SoftRemountStorage() to restore VFS access.
 *  @warning        File I/O via VFS will fail after this call until remounted.
 *                  Lock the filesystem before calling this to prevent concurrent writes.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized or storage not mounted
 *                  ESP_ERR_INVALID_ARG if unknown storage location
 */
esp_err_t MemoryManager_SoftUnmountStorage(void);

/** @brief          Soft-remount the active storage VFS filesystem.
 *                  Re-registers the storage with diskio, registers the VFS path, and mounts
 *                  the FAT filesystem using the preserved storage handles from a previous
 *                  MemoryManager_SoftUnmountStorage() call.
 *  @note           Must be called after USB MSC releases the storage.
 *                  Restores full VFS file I/O capability for the application.
 *  @warning        Must only be called after a successful MemoryManager_SoftUnmountStorage().
 *                  Do not call while USB MSC is still accessing the storage.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized or handles are invalid
 *                  ESP_ERR_INVALID_ARG if unknown storage location
 *                  ESP_FAIL if FAT filesystem mount fails
 */
esp_err_t MemoryManager_SoftRemountStorage(void);

/** @brief          Erase current storage location completely.
 *                  Deletes all files in the active storage filesystem (internal flash
 *                  or SD card) and reformats it.
 *  @note           Filesystem is automatically reformatted after erase.
 *                  All data loss is permanent - no recovery possible.
 *  @warning        This will delete ALL files including images and logs!
 *  @return         ESP_OK on success
 *                  ESP_ERR_NOT_FOUND if storage not found
 *                  ESP_FAIL if erase or format fails
 */
esp_err_t MemoryManager_EraseStorage(void);

/** @brief          Erase coredump partition completely.
 *                  Deletes any stored crash dumps from the dedicated coredump partition.
 *                  Useful for clearing old crash data or freeing partition space.
 *  @note           Partition will be cleared and ready for new coredumps.
 *                  Does not affect application data or settings.
 *  @warning        Crash dump data will be lost permanently.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NOT_FOUND if coredump partition not found
 *                  ESP_FAIL if erase fails
 */
esp_err_t MemoryManager_EraseCoredump(void);

/** @brief          Get wear leveling handle for internal storage.
 *                  Returns the wear leveling layer handle for direct access to
 *                  internal flash storage statistics and control.
 *  @note           Only valid for internal flash storage (not SD card).
 *                  Used for low-level wear leveling statistics.
 *  @param p_Handle Pointer to receive the wl_handle_t
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Handle is NULL
 *                  ESP_ERR_INVALID_STATE if not using internal storage
 *                  ESP_ERR_INVALID_STATE if storage not mounted
 */
esp_err_t MemoryManager_GetWearLevelingHandle(wl_handle_t *p_Handle);

/** @brief          Get SD card handle.
 *                  Returns pointer to sdmmc_card_t structure for direct SD card
 *                  access and card information queries.
 *  @note           Only valid when SD card storage is active.
 *                  Use for card info (capacity, speed, manufacturer).
 *  @param pp_Card  Pointer to receive the SD card handle pointer
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if pp_Card is NULL
 *                  ESP_ERR_INVALID_STATE if SD card not mounted
 */
esp_err_t MemoryManager_GetSDCardHandle(sdmmc_card_t **pp_Card);

/** @brief          Format the active storage location.
 *                  Reformats the currently active storage (internal flash or SD card).
 *  @note           Use with caution - all data will be lost. Use MemoryManager_EraseStorage() instead for safer erase.
 *  @warning        This will delete ALL files including images and logs!
 *  @return         ESP_OK on success
 *                  ESP_ERR_NOT_FOUND if storage not found
 *                  ESP_FAIL if format fails
 */
esp_err_t MemoryManager_FormatActiveStorage(void);

#endif /* MEMORYMANAGER_H_ */
