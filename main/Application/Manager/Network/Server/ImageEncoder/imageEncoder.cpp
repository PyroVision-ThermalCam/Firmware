/*
 * image_encoder.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Image encoder implementation (dispatcher for format-specific encoders).
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

#include "imageEncoder.h"
#include "JPEG/jpegEncoder.h"
#include "PNG/pngEncoder.h"
#include "Bitmap/bitmapEncoder.h"

#include "lepton.h"

typedef struct {
    bool isInitialized;
    uint8_t JpegQuality;
} Image_Image_Encoder_State_t;

static Image_Image_Encoder_State_t _Image_Encoder_State;

static const char *TAG = "Image-Encoder";

/** @brief          Apply color palette to thermal frame.
 *  @param p_Frame  Pointer to thermal frame (with RGB888 buffer)
 *  @param Palette  Color palette (currently ignored, frame already has correct colors)
 *  @param p_Output Output RGB buffer (width * height * 3 bytes)
 *  @return         ESP_OK on success
 */
static esp_err_t ImageEncoder_ApplyPalette(const Network_Thermal_Frame_t *p_Frame,
                                           Server_Palette_t Palette,
                                           uint8_t *p_Output)
{
    if ((p_Frame == NULL) || (p_Output == NULL) || (p_Frame->Buffer == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(p_Output, p_Frame->Buffer, p_Frame->Width * p_Frame->Height * 3);

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
                              Settings_Image_Format_t Format,
                              Server_Palette_t Palette,
                              Network_Encoded_Image_t *p_Encoded)
{
    esp_err_t Error;
    size_t PixelCount;
    size_t EncodedSize;
    uint8_t *p_RGB;
    uint8_t *p_EncodedData;
    uint32_t Caps;

    if ((p_Frame == NULL) || (p_Encoded == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(p_Encoded, 0, sizeof(Network_Encoded_Image_t));

    PixelCount = p_Frame->Width * p_Frame->Height;

    #ifdef CONFIG_SPIRAM
        Caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    #else
        Caps = MALLOC_CAP_8BIT;
    #endif

    /* Allocate temporary RGB buffer for palette application */
    p_RGB = static_cast<uint8_t *>(heap_caps_malloc(PixelCount * 3, Caps));
    if (p_RGB == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RGB buffer!");

        return ESP_ERR_NO_MEM;
    }

    /* Apply palette to frame */
    Error = ImageEncoder_ApplyPalette(p_Frame, Palette, p_RGB);
    if (Error != ESP_OK) {
        heap_caps_free(p_RGB);

        return Error;
    }

    /* Dispatch to appropriate encoder based on format */
    switch (Format) {
        case IMAGE_FORMAT_JPEG: {
            Error = JPEGEncoder_Encode(p_RGB, p_Frame->Width, p_Frame->Height,
                                       _Image_Encoder_State.JpegQuality,
                                       &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_JPEG;
                p_Encoded->Width = p_Frame->Width;
                p_Encoded->Height = p_Frame->Height;
            }

            break;
        }
        case IMAGE_FORMAT_PNG: {
            Error = PNGEncoder_Encode(p_RGB, p_Frame->Width, p_Frame->Height,
                                      &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_PNG;
                p_Encoded->Width = p_Frame->Width;
                p_Encoded->Height = p_Frame->Height;

                break;
            } else if (Error == ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGW(TAG, "PNG format not supported, falling back to RAW");
                /* Fallback to RAW format */
                Error = ESP_OK;
                p_Encoded->Data = p_RGB;
                p_Encoded->Size = PixelCount * 3;
                p_Encoded->Format = IMAGE_FORMAT_RAW;
                p_Encoded->Width = p_Frame->Width;
                p_Encoded->Height = p_Frame->Height;

                return ESP_OK;  /* Don't free p_RGB, it's now owned by p_Encoded */
            } else {
                /* Other errors */
                break;
            }
        }
        case IMAGE_FORMAT_BITMAP: {
            Error = BitmapEncoder_Encode(p_RGB, p_Frame->Width, p_Frame->Height,
                                         &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_BITMAP;
                p_Encoded->Width = p_Frame->Width;
                p_Encoded->Height = p_Frame->Height;
            }

            break;
        }
        case IMAGE_FORMAT_RAW:
        default: {
            p_Encoded->Data = p_RGB;
            p_Encoded->Size = PixelCount * 3;
            p_Encoded->Format = IMAGE_FORMAT_RAW;
            p_Encoded->Width = p_Frame->Width;
            p_Encoded->Height = p_Frame->Height;

            return ESP_OK;  /* Don't free p_RGB, it's now owned by p_Encoded */
        }
    }

    /* Free temporary RGB buffer (unless it was transferred to output) */
    heap_caps_free(p_RGB);

    return Error;
}

void ImageEncoder_Free(Network_Encoded_Image_t *p_Encoded)
{
    if (p_Encoded == NULL) {
        return;
    } else if (p_Encoded->Data != NULL) {
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
