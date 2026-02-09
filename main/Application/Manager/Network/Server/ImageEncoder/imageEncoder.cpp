/*
 * image_encoder.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Image encoder implementation.
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

#include <cstring>

#include "imageEncoder.h"

#include "lepton.h"

typedef struct {
    bool isInitialized;
    uint8_t JpegQuality;
} Image_Image_Encoder_State_t;

static Image_Image_Encoder_State_t _Image_Encoder_State;

static const char *TAG = "Image-Encoder";

/** @brief              Get palette lookup table.
 *  @param palette      Palette type.
 *  @return             Pointer to palette array.
 */
static const uint8_t (*ImageEncoder_GetPalette(Server_Palette_t palette))[3] {
    switch (palette)
    {
        case PALETTE_IRON:
            return Lepton_Palette_Iron;
        case PALETTE_RAINBOW:
            return Lepton_Palette_Rainbow;
        case PALETTE_GRAY:
        default:
            return NULL; /* Use grayscale formula */
    }
}

/** @brief          Apply color palette to thermal frame.
 *  @param p_Frame  Pointer to thermal frame (with RGB888 buffer)
 *  @param palette  Color palette (currently ignored, frame already has correct colors)
 *  @param p_Output Output RGB buffer (width * height * 3 bytes)
 *  @return         ESP_OK on success
 */
static esp_err_t ImageEncoder_ApplyPalette(const Network_Thermal_Frame_t *p_Frame,
                                           Server_Palette_t palette,
                                           uint8_t *p_Output)
{
    if ((p_Frame == NULL) || (p_Output == NULL) || (p_Frame->Buffer == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(p_Output, p_Frame->Buffer, p_Frame->Width * p_Frame->Height * 3);

    return ESP_OK;
}

/** @brief              Encode RGB data to JPEG.
 *  @param p_RGB        RGB pixel data
 *  @param Width        Image width
 *  @param Height       Image height
 *  @param Quality      JPEG quality (1-100)
 *  @param p_Encoded    Output encoded image
 *  @return             ESP_OK on success
 */
static esp_err_t ImageEncoder_EncodeJPEG(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                                         uint8_t Quality, Network_Encoded_Image_t *p_Encoded)
{
    jpeg_enc_config_t enc_config = {
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

    jpeg_enc_handle_t encoder = NULL;
    jpeg_error_t err = jpeg_enc_open(&enc_config, &encoder);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG encoder: %d!", err);
        return ESP_FAIL;
    }
    ;
    p_Encoded->Data = static_cast<uint8_t *>(heap_caps_malloc(Width * Height * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (p_Encoded->Data == NULL) {
        jpeg_enc_close(encoder);
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer!");
        return ESP_ERR_NO_MEM;
    }

    int out_size = 0;
    err = jpeg_enc_process(encoder, p_RGB, Width * Height * 3, p_Encoded->Data, Width * Height * 3, &out_size);

    jpeg_enc_close(encoder);

    if (err != JPEG_ERR_OK) {
        heap_caps_free(p_Encoded->Data);
        p_Encoded->Data = NULL;
        ESP_LOGE(TAG, "JPEG encoding failed: %d!", err);
        return ESP_FAIL;
    }

    p_Encoded->Size = out_size;
    p_Encoded->Format = NETWORK_IMAGE_FORMAT_JPEG;
    p_Encoded->Width = Width;
    p_Encoded->Height = Height;

    return ESP_OK;
}

esp_err_t ImageEncoder_Init(uint8_t Quality)
{
    if (_Image_Encoder_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing image encoder, quality=%d", Quality);

    _Image_Encoder_State.JpegQuality = Quality;
    if (_Image_Encoder_State.JpegQuality < 1) {
        _Image_Encoder_State.JpegQuality = 1;
    }

    if (_Image_Encoder_State.JpegQuality > 100) {
        _Image_Encoder_State.JpegQuality = 100;
    }

    _Image_Encoder_State.isInitialized = true;

    return ESP_OK;
}

void ImageEncoder_Deinit(void)
{
    if (_Image_Encoder_State.isInitialized == false) {
        return;
    }

    _Image_Encoder_State.isInitialized = false;

    ESP_LOGD(TAG, "Image encoder deinitialized");
}

esp_err_t ImageEncoder_Encode(const Network_Thermal_Frame_t *p_Frame,
                              Network_ImageFormat_t Format,
                              Server_Palette_t Palette,
                              Network_Encoded_Image_t *p_Encoded)
{
    esp_err_t Error;

    if ((p_Frame == NULL) || (p_Encoded == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(p_Encoded, 0, sizeof(Network_Encoded_Image_t));

    size_t pixel_count = p_Frame->Width * p_Frame->Height;

    uint8_t *rgb_buffer = static_cast<uint8_t *>(heap_caps_malloc(pixel_count * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (rgb_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RGB buffer!");
        return ESP_ERR_NO_MEM;
    }

    Error = ImageEncoder_ApplyPalette(p_Frame, Palette, rgb_buffer);
    if (Error != ESP_OK) {
        heap_caps_free(rgb_buffer);
        return Error;
    }

    switch (Format) {
        case NETWORK_IMAGE_FORMAT_JPEG: {
            Error = ImageEncoder_EncodeJPEG(rgb_buffer, p_Frame->Width, p_Frame->Height,
                                            _Image_Encoder_State.JpegQuality, p_Encoded);
            break;
        }
        case NETWORK_IMAGE_FORMAT_PNG: {
            /* PNG not implemented - fall through to RAW */
            ESP_LOGW(TAG, "PNG format not implemented, using RAW");
            /* Fall through */
        }
        case NETWORK_IMAGE_FORMAT_RAW:
        default: {
            /* Return raw RGB data */
            p_Encoded->Data = rgb_buffer;
            p_Encoded->Size = pixel_count * 3;
            p_Encoded->Format = NETWORK_IMAGE_FORMAT_RAW;
            p_Encoded->Width = p_Frame->Width;
            p_Encoded->Height = p_Frame->Height;

            return ESP_OK;
        }
    }

    heap_caps_free(rgb_buffer);

    return Error;
}

void ImageEncoder_Free(Network_Encoded_Image_t *p_Encoded)
{
    if (p_Encoded == NULL) {
        return;
    }

    if (p_Encoded->Data != NULL) {
        heap_caps_free(p_Encoded->Data);
        p_Encoded->Data = NULL;
    }

    p_Encoded->Size = 0;
}

void ImageEncoder_SetQuality(uint8_t Quality)
{
    _Image_Encoder_State.JpegQuality = Quality;
    if (_Image_Encoder_State.JpegQuality < 1) {
        _Image_Encoder_State.JpegQuality = 1;
    }

    if (_Image_Encoder_State.JpegQuality > 100) {
        _Image_Encoder_State.JpegQuality = 100;
    }

    ESP_LOGD(TAG, "JPEG quality set to %d", _Image_Encoder_State.JpegQuality);
}
