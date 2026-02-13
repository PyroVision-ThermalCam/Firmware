/*
 * jpegEncoder.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: JPEG encoder implementation.
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
#include <esp_jpeg_enc.h>

#include "jpegEncoder.h"

static const char *TAG = "JPEG-Encoder";

esp_err_t JPEGEncoder_Encode(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                             uint8_t Quality, uint8_t **p_Output, size_t *p_Size)
{
    int Size;
    jpeg_error_t Error;
    jpeg_enc_handle_t Encoder = NULL;
    jpeg_enc_config_t EncoderConfig = {
        .width = Width,
        .height = Height,
        .src_type = JPEG_PIXEL_FORMAT_RGB888,
        .subsampling = JPEG_SUBSAMPLE_420,
        .quality = Quality,
        .rotate = JPEG_ROTATE_0D,
        .task_enable = false,
        .hfm_task_priority = 0,
        .hfm_task_core = 0,
    };

    if ((p_RGB == NULL) || (p_Output == NULL) || (p_Size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if ((Width == 0) || (Height == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Clamp quality to valid range */
    if (Quality < 1) {
        Quality = 1;
    } else if (Quality > 100) {
        Quality = 100;
    }

    Error = jpeg_enc_open(&EncoderConfig, &Encoder);
    if (Error != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG encoder: %d!", Error);

        return ESP_FAIL;
    }

    *p_Output = static_cast<uint8_t *>(heap_caps_malloc(Width * Height * 3, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    if (*p_Output == NULL) {
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer!");

        jpeg_enc_close(Encoder);

        return ESP_ERR_NO_MEM;
    }

    Error = jpeg_enc_process(Encoder, p_RGB, Width * Height * 3, *p_Output, Width * Height * 3, &Size);

    jpeg_enc_close(Encoder);

    if (Error != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG encoding failed: %d!", Error);

        heap_caps_free(*p_Output);
        *p_Output = NULL;

        return ESP_FAIL;
    }

    *p_Size = Size;

    ESP_LOGD(TAG, "Encoded %dx%d JPEG image, quality=%d, size=%d bytes", Width, Height, Quality, Size);

    return ESP_OK;
}
