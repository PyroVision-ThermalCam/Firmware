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

/** @brief          Initialize the USB Manager as a composite USB device.
 *                  This function installs the TinyUSB driver with the composite descriptor
 *                  (UVC + CDC + MSC) and initializes all enabled USB classes in parallel.
 *                  Which classes are active depends on the flags in p_Config and the
 *                  corresponding sdkconfig entries (CONFIG_TINYUSB_xxx_ENABLED).
 *  @note           MSC requires a valid MountPoint when MSC_Enabled is true.
 *                  Storage must be mounted before calling this function.
 *                  Not thread-safe during initialization. Call once from USB control task.
 *  @warning        While USB MSC is active, the filesystem should not be accessed from
 *                  the application to avoid data corruption.
 *  @param p_Config Pointer to USB manager configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL or MountPoint is NULL while MSC_Enabled
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if TinyUSB driver installation fails
 */
esp_err_t USBManager_Init(const USB_Manager_Config_t *p_Config);

/** @brief      Deinitialize the USB Manager and stop all active USB classes.
 *              Stops MSC, UVC, and CDC modules, disconnects from the USB bus,
 *              and uninstalls the TinyUSB driver.
 *  @note       After deinitialization, the filesystem can be safely accessed again.
 *  @warning    Ensure the PC has safely ejected the USB drive before calling this function.
 *  @return     ESP_OK on success
 *              ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBManager_Deinit(void);

/** @brief  Check if USB Manager is initialized and active.
 *  @return true if USB Manager is initialized
 *          false if not initialized
 */
bool USBManager_IsInitialized(void);

/** @brief  Check if UVC has been enabled in the current USB configuration.
 *  @return true if UVC is enabled and initialized
 *          false otherwise
 */
bool USBManager_IsUVCEnabled(void);

/** @brief  Check if CDC has been enabled in the current USB configuration.
 *  @return true if CDC is enabled and initialized
 *          false otherwise
 */
bool USBManager_IsCDCEnabled(void);

/** @brief  Check if MSC has been enabled in the current USB configuration.
 *  @return true if MSC is enabled and initialized
 *          false otherwise
 */
bool USBManager_IsMSCEnabled(void);

#endif /* USB_MANAGER_H_ */
