/*
 * bitmapEncoder.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Bitmap (BMP) encoder implementation.
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

#include <esp_log.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "bitmapEncoder.h"

/** @brief BMP file header structure (14 bytes).
 */
typedef struct {
    uint16_t Type;              /**< File type, must be 0x4D42 ('BM') */
    uint32_t Size;              /**< File size in bytes */
    uint16_t Reserved1;         /**< Reserved, must be 0 */
    uint16_t Reserved2;         /**< Reserved, must be 0 */
    uint32_t OffBits;           /**< Offset to pixel data from beginning of file */
} __attribute__((packed)) BMP_FileHeader_t;

/** @brief BMP info header structure (40 bytes, BITMAPINFOHEADER).
 */
typedef struct {
    uint32_t Size;              /**< Size of this header (40 bytes) */
    int32_t Width;              /**< Image width in pixels */
    int32_t Height;             /**< Image height in pixels (positive = bottom-up) */
    uint16_t Planes;            /**< Number of color planes, must be 1 */
    uint16_t BitCount;          /**< Bits per pixel (24 for RGB888) */
    uint32_t Compression;       /**< Compression method (0 = uncompressed) */
    uint32_t SizeImage;         /**< Size of raw bitmap data (can be 0 for uncompressed) */
    int32_t XPelsPerMeter;      /**< Horizontal resolution (pixels/meter) */
    int32_t YPelsPerMeter;      /**< Vertical resolution (pixels/meter) */
    uint32_t ClrUsed;           /**< Number of colors in palette (0 = default) */
    uint32_t ClrImportant;      /**< Number of important colors (0 = all) */
} __attribute__((packed)) BMP_InfoHeader_t;

static const char *TAG = "Bitmap-Encoder";

esp_err_t BitmapEncoder_Encode(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                               uint8_t **p_Output, size_t *p_Size)
{
    BMP_FileHeader_t FileHeader;
    BMP_InfoHeader_t InfoHeader;
    uint32_t RowSize;
    uint32_t PixelDataSize;
    uint32_t TotalSize;
    uint32_t Caps;
    uint8_t *p_BMP;
    uint8_t *p_Dest;
    const uint8_t *p_Src;

    if ((p_RGB == NULL) || (p_Output == NULL) || (p_Size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if ((Width == 0) || (Height == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate row size (must be multiple of 4 bytes) */
    RowSize = ((Width * 3 + 3) / 4) * 4;
    PixelDataSize = RowSize * Height;
    TotalSize = sizeof(BMP_FileHeader_t) + sizeof(BMP_InfoHeader_t) + PixelDataSize;

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    Caps = MALLOC_CAP_8BIT;
#endif

    /* Allocate output buffer */
    p_BMP = static_cast<uint8_t *>(heap_caps_malloc(TotalSize, Caps));
    if (p_BMP == NULL) {
        ESP_LOGE(TAG, "Failed to allocate BMP output buffer (%lu bytes)!", TotalSize);

        return ESP_ERR_NO_MEM;
    }

    /* Fill file header */
    memset(&FileHeader, 0, sizeof(BMP_FileHeader_t));
    FileHeader.Type = 0x4D42;                                           /* 'BM' */
    FileHeader.Size = TotalSize;
    FileHeader.OffBits = sizeof(BMP_FileHeader_t) + sizeof(BMP_InfoHeader_t);

    /* Fill info header */
    memset(&InfoHeader, 0, sizeof(BMP_InfoHeader_t));
    InfoHeader.Size = sizeof(BMP_InfoHeader_t);
    InfoHeader.Width = Width;
    InfoHeader.Height = Height;                                         /* Positive = bottom-up */
    InfoHeader.Planes = 1;
    InfoHeader.BitCount = 24;                                           /* 24-bit RGB */
    InfoHeader.Compression = 0;                                         /* Uncompressed */
    InfoHeader.SizeImage = PixelDataSize;
    InfoHeader.XPelsPerMeter = 2835;                                    /* 72 DPI */
    InfoHeader.YPelsPerMeter = 2835;
    InfoHeader.ClrUsed = 0;
    InfoHeader.ClrImportant = 0;

    /* Write headers to output buffer */
    memcpy(p_BMP, &FileHeader, sizeof(BMP_FileHeader_t));
    memcpy(p_BMP + sizeof(BMP_FileHeader_t), &InfoHeader, sizeof(BMP_InfoHeader_t));

    /* Convert RGB to BGR and write pixel data (bottom-to-top) */
    p_Dest = p_BMP + sizeof(BMP_FileHeader_t) + sizeof(BMP_InfoHeader_t);

    for (int32_t y = Height - 1; y >= 0; y--) {
        p_Src = p_RGB + (y * Width * 3);

        for (uint16_t x = 0; x < Width; x++) {
            /* BMP uses BGR order instead of RGB */
            p_Dest[x * 3 + 0] = p_Src[x * 3 + 2];  /* Blue */
            p_Dest[x * 3 + 1] = p_Src[x * 3 + 1];  /* Green */
            p_Dest[x * 3 + 2] = p_Src[x * 3 + 0];  /* Red */
        }

        /* Add row padding (if needed) */
        uint32_t Padding = RowSize - (Width * 3);
        if (Padding > 0) {
            memset(p_Dest + (Width * 3), 0, Padding);
        }

        p_Dest += RowSize;
    }

    *p_Output = p_BMP;
    *p_Size = TotalSize;

    ESP_LOGD(TAG, "Encoded %dx%d BMP image, size=%lu bytes", Width, Height, TotalSize);

    return ESP_OK;
}
