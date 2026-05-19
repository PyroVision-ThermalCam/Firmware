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

#define USB_ERR_BASE                     0x6000

/** @defgroup USB_ERRORS USB Manager Error Codes
 *  @brief Error codes returned by USBManager functions (base: @c USB_ERR_BASE = 0x6000).
 *  @{
 */

/** @brief USBManager_Init() has not been called yet. */
#define USB_ERR_NOT_INITIALIZED                 (USB_ERR_BASE + 0x01)

/** @brief USBManager_Init() called more than once (already initialised). */
#define USB_ERR_ALREADY_INITIALIZED             (USB_ERR_BASE + 0x02)

/** @brief TinyUSB driver installation (tinyusb_driver_install) failed. */
#define USB_ERR_DRIVER_INSTALL                  (USB_ERR_BASE + 0x03)

/** @brief CDC class initialisation failed. */
#define USB_ERR_CDC_INIT                        (USB_ERR_BASE + 0x04)

/** @brief Internal command queue creation failed. */
#define USB_ERR_QUEUE_CREATE                    (USB_ERR_BASE + 0x05)

/** @brief Command queue is full — command could not be enqueued (caller should retry). */
#define USB_ERR_QUEUE_FULL                      (USB_ERR_BASE + 0x06)

/** @brief MSC storage path unavailable — MemoryManager_Init() must be called first. */
#define USB_ERR_MSC_STORAGE_UNAVAILABLE         (USB_ERR_BASE + 0x07)

/** @brief MSC class initialisation failed. */
#define USB_ERR_MSC_INIT                        (USB_ERR_BASE + 0x08)

/** @brief UVC class initialisation failed. */
#define USB_ERR_UVC_INIT                        (USB_ERR_BASE + 0x09)

/** @brief USB monitoring task creation failed. */
#define USB_ERR_TASK_CREATE                     (USB_ERR_BASE + 0x0A)

/** @} */

/** @brief USB Manager events base.
 */
ESP_EVENT_DECLARE_BASE(USB_EVENTS);

/** @brief USB Manager event IDs.
 */
typedef enum {
    USB_EVENT_INITIALIZED,              /**< USB subsystem (TinyUSB driver + CDC) initialized and ready. */
    USB_EVENT_UNINITIALIZED,            /**< USB subsystem uninitialized and stopped. */
    USB_EVENT_CABLE_CONNECTED,          /**< USB cable connected and enumerated by host. */
    USB_EVENT_CABLE_DISCONNECTED,       /**< USB cable disconnected from host. */
    USB_EVENT_UVC_STREAMING_START,      /**< UVC streaming started by USB host. */
    USB_EVENT_UVC_STREAMING_STOP,       /**< UVC streaming stopped by USB host. */
    USB_EVENT_CDC_CONNECTED,            /**< CDC host terminal connected (DTR+RTS set). */
    USB_EVENT_CDC_DISCONNECTED,         /**< CDC host terminal disconnected. */
} USB_Event_ID_t;

#endif /* USB_TYPES_H_ */
