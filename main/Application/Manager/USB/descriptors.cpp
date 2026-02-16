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

#include <class/msc/msc.h>
#include <class/video/video.h>

#include <string.h>

#include "descriptors.h"
#include "uvc_descriptors.h"

#include <sdkconfig.h>

#define CONFIG_MSC_TOTAL_LEN            (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define CONFIG_UVC_TOTAL_LEN            (TUD_CONFIG_DESC_LEN + TUD_VIDEO_CAPTURE_DESC_MJPEG_LEN)

/* MSC Endpoint numbers */
#define EPNUM_MSC_OUT                   0x01
#define EPNUM_MSC_IN                    0x81

/* USB Vendor and Product IDs */
#define CONFIG_USB_VID                  0x1234
#define CONFIG_USB_PID                  0x5678

/* UVC Endpoint numbers */
#define EPNUM_UVC_VIDEO_IN              0x01

/* MSC Configuration Descriptor */
enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_MSC_TOTAL
};

/* UVC Configuration Descriptor */
enum {
    ITF_NUM_UVC_VIDEO_CONTROL = 0,
    ITF_NUM_UVC_VIDEO_STREAMING,
    ITF_NUM_UVC_TOTAL
};

static tusb_desc_device_t const Desc_Device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,                                   // USB 2.0
    .bDeviceClass       = 0,                                        // Class defined at interface level
    .bDeviceSubClass    = 0,                                        // Subclass defined at interface level
    .bDeviceProtocol    = 0,                                        // Protocol defined at interface level
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = CONFIG_USB_VID,
    .idProduct          = CONFIG_USB_PID,
    .bcdDevice          = 0x0100,                                   // Device release 1.0
    .iManufacturer      = 0x01,                                     // Index of manufacturer string
    .iProduct           = 0x02,                                     // Index of product string
    .iSerialNumber      = 0x00,                                     // Index of serial number string
    .bNumConfigurations = 0x01,                                     // One configuration
};

uint8_t const *get_Desc_Device(void)
{
    return static_cast<uint8_t const *>(static_cast<void const *>(&Desc_Device));
}

static uint8_t const Desc_Config_MSC[] = {
    /* Config descriptor */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_MSC_TOTAL, 0, CONFIG_MSC_TOTAL_LEN, 0, 100),

    /* MSC descriptor */
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static uint8_t const Desc_Config_UVC[] = {
    /* Config descriptor */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_UVC_TOTAL, 0, CONFIG_UVC_TOTAL_LEN, 0, 100),

    /* UVC descriptor (MJPEG, Isochronous, single frame) */
    TUD_VIDEO_CAPTURE_DESCRIPTOR_MJPEG(0, ITF_NUM_UVC_VIDEO_CONTROL,
                                       0x80 | EPNUM_UVC_VIDEO_IN,
                                       CONFIG_TINYUSB_UVC_CAM1_FRAMESIZE_WIDTH,
                                       CONFIG_TINYUSB_UVC_CAM1_FRAMESIZE_HEIGHT,
                                       CONFIG_TINYUSB_UVC_CAM1_FRAMERATE,
                                       CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE),
};

uint8_t const *get_Desc_Config_UVC(void)
{
    return Desc_Config_UVC;
}

uint8_t const *get_Desc_Config_MSC(void)
{
    return Desc_Config_MSC;
}

