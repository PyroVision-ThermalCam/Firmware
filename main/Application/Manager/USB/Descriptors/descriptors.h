/*
 * descriptors.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB composite device descriptor definitions (UVC + CDC + MSC).
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

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <tinyusb.h>

/** @brief String Descriptor Index.
 */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

/** @brief  Get the USB device descriptor.
 *          Returns the device descriptor for the composite USB device
 *          (MISC class with IAD protocol for UVC + CDC + MSC).
 *  @return Pointer to the device descriptor structure
 */
const tusb_desc_device_t *get_Desc_Device(void);

/** @brief  Get the USB composite configuration descriptor.
 *          Returns the full configuration descriptor including all enabled
 *          class interfaces (UVC, CDC, MSC) as selected by sdkconfig.
 *  @return Pointer to the configuration descriptor byte array
 */
const uint8_t *get_Desc_Config(void);

#endif /* USB_DESCRIPTORS_H_ */
