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

/** @brief          Initialize the USB Manager.
 *                  Installs the TinyUSB driver with the composite descriptor, initializes the CDC
 *                  class at boot time, creates an internal command queue and starts a monitoring
 *                  task. The monitoring task polls the USB cable connection state and posts
 *                  USB_EVENT_CABLE_CONNECTED or USB_EVENT_CABLE_DISCONNECTED via the ESP event
 *                  system when the state changes. MSC and UVC classes are NOT enabled at init;
 *                  use USBManager_EnableMSC() and USBManager_EnableUVC() to activate them.
 *  @note           Must be called after MemoryManager_Init() so the storage path is available.
 *                  Call once during boot before starting application tasks.
 *  @warning        MSC requires the filesystem to be mounted before USBManager_EnableMSC(true)
 *                  is called.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_ERR_NO_MEM if queue or task creation fails
 *                  ESP_FAIL if TinyUSB driver installation or CDC initialization fails
 */
esp_err_t USBManager_Init(void);

/** @brief          Deinitialize the USB Manager.
 *                  Disables all active USB classes, disconnects from the USB bus, stops the
 *                  internal monitoring task, and uninstalls the TinyUSB driver.
 *  @note           After deinitialization the filesystem is accessible again.
 *  @warning        Ensure the host has safely ejected the USB drive before calling this function.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBManager_Deinit(void);

/** @brief          Enable or disable the USB Mass Storage Class.
 *                  Posts a command to the internal USB Manager task and returns immediately
 *                  without blocking the caller. The actual USBMSC_Init() or USBMSC_Deinit()
 *                  operation runs in the USB Manager task context.
 *  @note           Call only when USBManager_IsInitialized() returns true.
 *                  MSC requires a mounted filesystem at MemoryManager_GetStoragePath().
 *                  Enable is silently ignored when the cable is not connected.
 *  @warning        While MSC is active the application must not write to the filesystem.
 *  @param Enable   true to enable MSC, false to disable
 *  @return         ESP_OK if command was enqueued successfully
 *                  ESP_ERR_INVALID_STATE if USB Manager is not initialized
 *                  ESP_ERR_TIMEOUT if command queue is full
 */
esp_err_t USBManager_EnableMSC(bool Enable);

/** @brief          Enable or disable the USB Video Class.
 *                  Posts a command to the internal USB Manager task and returns immediately
 *                  without blocking the caller. The actual USBUVC_Init() or USBUVC_Deinit()
 *                  operation runs in the USB Manager task context.
 *  @note           Call only when USBManager_IsInitialized() returns true.
 *                  Enable is silently ignored when the cable is not connected.
 *  @param Enable   true to enable UVC, false to disable
 *  @return         ESP_OK if command was enqueued successfully
 *                  ESP_ERR_INVALID_STATE if USB Manager is not initialized
 *                  ESP_ERR_TIMEOUT if command queue is full
 */
esp_err_t USBManager_EnableUVC(bool Enable);

/** @brief  Check if the USB Manager is initialized and the TinyUSB driver is active.
 *  @return true if USB Manager is initialized
 *          false if not initialized
 */
bool USBManager_IsInitialized(void);

/** @brief  Check if a USB cable is currently connected and enumerated by the host.
 *  @return true if USB cable is connected and device has been enumerated by the host
 *          false if cable is absent or device has not yet been enumerated
 */
bool USBManager_IsCableConnected(void);

#endif /* USB_MANAGER_H_ */
