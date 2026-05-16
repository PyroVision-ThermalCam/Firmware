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
#ifdef CONFIG_IMAGE_ENCODER_JPEG
#include "JPEG/jpegEncoder.h"
#endif
#ifdef CONFIG_IMAGE_ENCODER_PNG
#include "PNG/pngEncoder.h"
#endif
#ifdef CONFIG_IMAGE_ENCODER_BITMAP
#include "Bitmap/bitmapEncoder.h"
#endif

typedef struct {
    bool IsInitialized;
    uint8_t JpegQuality;
} ImageEncoder_State_t;

static ImageEncoder_State_t _ImageEncoderState;

static const char *TAG = "Image-Encoder";

esp_err_t ImageEncoder_Init(uint8_t Quality)
{
    if (_ImageEncoderState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing image encoder, quality=%d", Quality);

    _ImageEncoderState.JpegQuality = Quality;
    if (_ImageEncoderState.JpegQuality < 1) {
        _ImageEncoderState.JpegQuality = 1;
    }

    if (_ImageEncoderState.JpegQuality > 100) {
        _ImageEncoderState.JpegQuality = 100;
    }

    _ImageEncoderState.IsInitialized = true;

    return ESP_OK;
}

void ImageEncoder_Deinit(void)
{
    if (_ImageEncoderState.IsInitialized == false) {
        return;
    }

    _ImageEncoderState.IsInitialized = false;

    ESP_LOGD(TAG, "Image encoder deinitialized");
}

esp_err_t ImageEncoder_Encode(const ImageEncoder_Raw_t *p_Frame,
                              ImageEncoder_Format_t Format,
                              ImageEncoder_EncodedImage_t *p_Encoded)
{
    esp_err_t Error;
    size_t EncodedSize;
    uint8_t *p_EncodedData;

    if ((p_Frame == NULL) || (p_Encoded == NULL)) {
        return IMAGE_ENCODER_ERR_INVALID_ARG;
    } else if (p_Frame->Buffer == NULL) {
        return IMAGE_ENCODER_ERR_INVALID_ARG;
    }

    memset(p_Encoded, 0, sizeof(ImageEncoder_EncodedImage_t));

    uint16_t EncodeWidth = p_Frame->Width;
    uint16_t EncodeHeight = p_Frame->Height;

    /* Dispatch to appropriate encoder based on format.
     * Always use the pre-rendered RGB888 display buffer directly — no palette re-application. */
    switch (Format) {
        case IMAGE_FORMAT_JPEG: {
#ifdef CONFIG_IMAGE_ENCODER_JPEG
            Error = JPEGEncoder_Encode(p_Frame->Buffer, EncodeWidth, EncodeHeight,
                                       _ImageEncoderState.JpegQuality, JPEG_ROTATE_0D,
                                       &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_JPEG;
                p_Encoded->Width = EncodeWidth;
                p_Encoded->Height = EncodeHeight;
            }
#else
            ESP_LOGW(TAG, "JPEG encoder disabled at build time");
            Error = ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        }
        case IMAGE_FORMAT_PNG: {
#ifdef CONFIG_IMAGE_ENCODER_PNG
            Error = PNGEncoder_Encode(p_Frame->Buffer, EncodeWidth, EncodeHeight,
                                      &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_PNG;
                p_Encoded->Width = EncodeWidth;
                p_Encoded->Height = EncodeHeight;

                break;
            } else if (Error == ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGW(TAG, "PNG format not supported, falling back to RAW");
                /* Fallback to RAW: allocate a copy since ImageEncoder_Free will free the data. */
                size_t PixelCount = static_cast<size_t>(EncodeWidth) * EncodeHeight;
                uint8_t *p_RawCopy = static_cast<uint8_t *>(heap_caps_malloc(PixelCount * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (p_RawCopy == NULL) {
                    return ESP_ERR_NO_MEM;
                }
                memcpy(p_RawCopy, p_Frame->Buffer, PixelCount * 3);
                p_Encoded->Data = p_RawCopy;
                p_Encoded->Size = PixelCount * 3;
                p_Encoded->Format = IMAGE_FORMAT_RAW;
                p_Encoded->Width = EncodeWidth;
                p_Encoded->Height = EncodeHeight;

                return ESP_OK;
            } else {
                /* Other errors */
                break;
            }
#else
            ESP_LOGW(TAG, "PNG encoder disabled at build time");
            Error = ESP_ERR_NOT_SUPPORTED;
            break;
#endif
        }
        case IMAGE_FORMAT_BITMAP: {
#ifdef CONFIG_IMAGE_ENCODER_BITMAP
            Error = BitmapEncoder_Encode(p_Frame->Buffer, EncodeWidth, EncodeHeight,
                                         &p_EncodedData, &EncodedSize);
            if (Error == ESP_OK) {
                p_Encoded->Data = p_EncodedData;
                p_Encoded->Size = EncodedSize;
                p_Encoded->Format = IMAGE_FORMAT_BITMAP;
                p_Encoded->Width = EncodeWidth;
                p_Encoded->Height = EncodeHeight;
            }
#else
            ESP_LOGW(TAG, "Bitmap encoder disabled at build time");
            Error = ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        }
        case IMAGE_FORMAT_RAW:
        default: {
#ifdef CONFIG_IMAGE_ENCODER_RAW
            /* Allocate a copy since ImageEncoder_Free will free the output data. */
            size_t PixelCount = static_cast<size_t>(EncodeWidth) * EncodeHeight;
            uint8_t *p_RawCopy = static_cast<uint8_t *>(heap_caps_malloc(PixelCount * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (p_RawCopy == NULL) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(p_RawCopy, p_Frame->Buffer, PixelCount * 3);
            p_Encoded->Data = p_RawCopy;
            p_Encoded->Size = PixelCount * 3;
            p_Encoded->Format = IMAGE_FORMAT_RAW;
            p_Encoded->Width = EncodeWidth;
            p_Encoded->Height = EncodeHeight;

            return ESP_OK;
#else
            ESP_LOGW(TAG, "RAW encoder disabled at build time");
            Error = ESP_ERR_NOT_SUPPORTED;
            break;
#endif
        }
    }

    return Error;
}

void ImageEncoder_Free(ImageEncoder_EncodedImage_t *p_Encoded)
{
    if (p_Encoded == NULL) {
        return;
    } else if (p_Encoded->Data != NULL) {
        heap_caps_free(p_Encoded->Data);
        p_Encoded->Data = NULL;
    }

    p_Encoded->Size = 0;
}

#ifdef CONFIG_IMAGE_ENCODER_JPEG
void ImageEncoder_SetQuality(uint8_t Quality)
{
    _ImageEncoderState.JpegQuality = Quality;
    if (_ImageEncoderState.JpegQuality < 1) {
        _ImageEncoderState.JpegQuality = 1;
    }

    if (_ImageEncoderState.JpegQuality > 100) {
        _ImageEncoderState.JpegQuality = 100;
    }

    ESP_LOGD(TAG, "JPEG quality set to %d", _ImageEncoderState.JpegQuality);
}
#endif