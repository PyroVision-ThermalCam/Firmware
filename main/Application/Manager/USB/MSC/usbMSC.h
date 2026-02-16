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
 *                  Configures TinyUSB MSC interface and exposes storage to host PC.
 *                  Storage is auto-detected (SD card or internal flash).
 *  @note           Storage must be mounted before calling this function.
 *                  Filesystem will be locked during MSC operation.
 *  @warning        Not thread-safe. Call once during initialization.
 *  @param p_Config Pointer to MSC configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL or invalid
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if MSC initialization fails
 */
esp_err_t USBMSC_Init(const USB_MSC_Config_t *p_Config);

/** @brief      Deinitialize USB Mass Storage Class device.
 *              Stops MSC operation and unlocks filesystem.
 *  @note       Waits for host to safely eject device.
 *  @warning    Ensure host has ejected device before calling.
 *  @return     ESP_OK on success
 *              ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBMSC_Deinit(void);

/** @brief  Check if USB MSC is initialized and active.
 *  @return true if MSC active
 *          false if not initialized
 */
bool USBMSC_IsInitialized(void);

#endif /* USB_MSC_H_ */
