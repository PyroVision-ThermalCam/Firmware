/*
 * usbMSC.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Mass Storage Class module definition.
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

#ifndef USB_MSC_H_
#define USB_MSC_H_

#include "usbMSCTypes.h"

/** @brief          Initialize USB Mass Storage Class device.
 *                  Locks the filesystem, soft-unmounts the VFS FAT layer, and creates a
 *                  TinyUSB MSC storage with TINYUSB_MSC_STORAGE_MOUNT_USB to expose
 *                  raw storage blocks to the USB host PC. The PC can then read and write
 *                  the FAT filesystem directly. The application VFS path becomes
 *                  inaccessible until USBMSC_Deinit() is called.
 *  @note           Storage must be mounted via MemoryManager before calling this function.
 *                  The storage type (SD card or internal flash) is auto-detected.
 *                  On failure, VFS is automatically re-mounted and filesystem unlocked.
 *  @warning        Not thread-safe. Call from the USB Manager task context only.
 *                  Application file I/O will fail while MSC is active.
 *  @param p_Config Pointer to MSC configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL or MountPoint is NULL
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if MSC storage creation fails
 */
esp_err_t USBMSC_Init(const USB_MSC_Config_t *p_Config);

/** @brief          Deinitialize USB Mass Storage Class device.
 *                  Deletes the TinyUSB MSC storage, soft-remounts the VFS FAT layer to
 *                  restore application file I/O access, and unlocks the filesystem.
 *                  After this call, the camera can save images and access files again.
 *  @note           Waits for host to safely eject device before calling.
 *                  The storage handles (WL or SD card) remain valid throughout.
 *  @warning        Ensure host has ejected the USB drive before calling.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBMSC_Deinit(void);

/** @brief  Check if USB MSC is initialized and active.
 *  @return true if MSC active
 *          false if not initialized
 */
bool USBMSC_IsInitialized(void);

#endif /* USB_MSC_H_ */
