/*
 * descriptors.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Descriptors definition.
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

#ifndef DESCRIPTORS_H_
#define DESCRIPTORS_H_

#include <tinyusb.h>

#include "usbTypes.h"

/** @brief String Descriptor Index
 */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

/** @brief  Get the device descriptor. This is used by the USB Manager to provide the device descriptor to TinyUSB.
 *  @return Pointer to the device descriptor structure.
 */
uint8_t const *get_Desc_Device(void);

/** @brief          Get the MSC configuration descriptor.
 *  @return         Pointer to the MSC configuration descriptor.
 */
uint8_t const *get_Desc_Config_MSC(void);

/** @brief          Get the UVC configuration descriptor.
 *                  Returns a UVC-only configuration descriptor (MJPEG, Isochronous)
 *                  that does not include any MSC interfaces.
 *  @return         Pointer to the UVC configuration descriptor.
 */
uint8_t const *get_Desc_Config_UVC(void);

#endif /* DESCRIPTORS_H_ */
