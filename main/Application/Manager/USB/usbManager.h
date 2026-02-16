/*
 * usbManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Manager definition.
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

#ifndef USB_MANAGER_H_
#define USB_MANAGER_H_

#include "usbTypes.h"

/** @brief          Initialize the USB Manager and expose storage as USB Mass Storage Device.
 *                  This function initializes the TinyUSB stack, configures the USB descriptors,
 *                  and exposes the storage via USB as a Mass Storage Device.
 *  @note           Storage must be mounted at p_Config->MountPoint before calling this function.
 *                  This function blocks and starts the TinyUSB task.
 *  @warning        While USB MSC is active, the filesystem should not be accessed from the application
 *                  to avoid data corruption. The PC has exclusive access.
 *                  Not thread-safe during initialization. Call once from main task.
 *  @param p_Config Pointer to USB manager configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL or contains invalid parameters
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if USB initialization fails
 */
esp_err_t USBManager_Init(const USB_Manager_Config_t *p_Config);

/** @brief      Deinitialize the USB Manager and stop USB Mass Storage Device.
 *              Stops the TinyUSB stack and releases all resources. After calling this function,
 *              the storage is no longer accessible via USB.
 *  @note       After deinitializing, the filesystem can be safely accessed by the application again.
 *  @warning    Ensure the PC has safely ejected the USB drive before calling this function.
 *  @return     ESP_OK on success
 *              ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBManager_Deinit(void);

/** @brief  Check if USB Manager is initialized and active.
 *  @return true if USB MSC is active
 *          false if not initialized
 */
bool USBManager_IsInitialized(void);

#endif /* USB_MANAGER_H_ */
