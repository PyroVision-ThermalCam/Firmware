/*
 * usbMSCTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Mass Storage Class type definitions.
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

#ifndef USB_MSC_TYPES_H_
#define USB_MSC_TYPES_H_

#include <esp_err.h>

#include <stdint.h>

/** @brief USB MSC configuration structure.
 */
typedef struct {
    const char *MountPoint;         /**< Mount point of the storage (must match MemoryManager mount point). */
} USB_MSC_Config_t;

#endif /* USB_MSC_TYPES_H_ */
