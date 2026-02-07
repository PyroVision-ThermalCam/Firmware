/*
 * descriptors.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB Descriptors implementation.
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

#include "descriptors.h"

static tusb_desc_device_t const Desc_Device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,                                   // USB 2.0
    .bDeviceClass       = 0x00,                                     // Defined in interface descriptor
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = CONFIG_USB_VID,
    .idProduct          = CONFIG_USB_PID,
    .bcdDevice          = 0x0100,                                   // Device release 1.0
    .iManufacturer      = 0x01,                                     // Index of manufacturer string
    .iProduct           = 0x02,                                     // Index of product string
    .iSerialNumber      = 0x03,                                     // Index of serial number string
    .bNumConfigurations = 0x01,                                     // One configuration
};

uint8_t const *get_Desc_Device(void)
{
    return (uint8_t const *)&Desc_Device;
}