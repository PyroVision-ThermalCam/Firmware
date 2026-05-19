/*
 * imageEncoderTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Image encoder for captured images.
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

#ifndef IMAGE_ENCODER_TYPES_H_
#define IMAGE_ENCODER_TYPES_H_

/** @brief Image format types.
 */
typedef enum {
#ifdef CONFIG_IMAGE_ENCODER_JPEG
    IMAGE_FORMAT_JPEG    = 0,                   /**< JPEG format. */
#endif
#ifdef CONFIG_IMAGE_ENCODER_PNG
    IMAGE_FORMAT_PNG     = 1,                   /**< PNG format. */
#endif
#ifdef CONFIG_IMAGE_ENCODER_RAW
    IMAGE_FORMAT_RAW     = 2,                   /**< Raw format. */
#endif
#ifdef CONFIG_IMAGE_ENCODER_BITMAP
    IMAGE_FORMAT_BITMAP  = 3,                   /**< Bitmap (BMP) format. */
#endif
    IMAGE_FORMAT_COUNT   = 4                    /**< Total number of format entries. */
} ImageEncoder_Format_t;

/** @brief Lookup table for supported format names (index corresponds to format ID).
 */
static const char* const ImageEncoder_Format_Names[IMAGE_FORMAT_COUNT] = {
#ifdef CONFIG_IMAGE_ENCODER_JPEG
    [IMAGE_FORMAT_JPEG]   = "JPEG",
#endif
#ifdef CONFIG_IMAGE_ENCODER_PNG
    [IMAGE_FORMAT_PNG]    = "PNG",
#endif
#ifdef CONFIG_IMAGE_ENCODER_RAW
    [IMAGE_FORMAT_RAW]    = "RAW",
#endif
#ifdef CONFIG_IMAGE_ENCODER_BITMAP
    [IMAGE_FORMAT_BITMAP] = "BITMAP",
#endif
};

/** @brief Input image data structure.
 */
typedef struct {
    uint8_t *Buffer;                    /**< Pointer to RGB888 image data */
    uint16_t *RawBuffer;                /**< Pointer to raw 14-bit Lepton pixel data; NULL if not available. */
    uint16_t Width;                     /**< Frame width in pixels */
    uint16_t Height;                    /**< Frame height in pixels */
    uint16_t RawWidth;                  /**< Width of the raw buffer in pixels (Lepton native resolution). */
    uint16_t RawHeight;                 /**< Height of the raw buffer in pixels (Lepton native resolution). */
    uint16_t RawMin;                    /**< Minimum raw pixel value in the frame (for palette normalization). */
    uint16_t RawMax;                    /**< Maximum raw pixel value in the frame (for palette normalization). */
    uint32_t Timestamp;                 /**< Timestamp in milliseconds */
    SemaphoreHandle_t Mutex;            /**< Mutex for thread-safe access */
} ImageEncoder_Raw_t;

/** @brief Encoded image data.
 */
typedef struct {
    uint8_t *Data;                      /**< Encoded image data */
    size_t Size;                        /**< Size of encoded data */
    ImageEncoder_Format_t Format;       /**< Image format */
    uint16_t Width;                     /**< Image width */
    uint16_t Height;                    /**< Image height */
} ImageEncoder_EncodedImage_t;

#endif /* IMAGE_ENCODER_TYPES_H_ */