/*
 * usbTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the USB Manager component.
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

#ifndef USB_TYPES_H_
#define USB_TYPES_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

/** @brief USB Manager events base.
 */
ESP_EVENT_DECLARE_BASE(USB_EVENTS);

/** @brief USB Manager mode types.
 */
typedef enum {
    USB_MODE_MSC = 0,                       /**< USB Mass Storage Class mode. */
    USB_MODE_UVC,                           /**< USB Video Class mode. */
} USB_Mode_t;

/** @brief USB Manager event IDs.
 */
typedef enum {
    USB_EVENT_INITIALIZED,           /**< USB subsystem initialized and ready. */
    USB_EVENT_UNINITIALIZED,         /**< USB subsystem uninitialized and stopped. */
    USB_EVENT_UVC_STREAMING_START,   /**< UVC streaming started by USB host. */
    USB_EVENT_UVC_STREAMING_STOP,    /**< UVC streaming stopped by USB host. */
} USB_Event_ID_t;

/** @brief USB Manager configuration structure.
 *  @note  Storage type is automatically detected from MemoryManager.
 */
typedef struct {
    USB_Mode_t Mode;                        /**< USB mode. */
    const char *MountPoint;                 /**< Mount point of the storage (must match MemoryManager mount point). */
} USB_Manager_Config_t;

#endif /* USB_TYPES_H_ */
