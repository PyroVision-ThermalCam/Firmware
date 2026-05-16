/*
 * jpegEncoder.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: JPEG encoder for thermal images.
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

#ifndef JPEG_ENCODER_H_
#define JPEG_ENCODER_H_

#include <esp_err.h>
#include <esp_jpeg_common.h>

#include <stdint.h>
#include <stddef.h>

/** @brief              Encode RGB888 data to JPEG format.
 *  @param p_RGB        Pointer to RGB888 pixel data (width * height * 3 bytes)
 *  @param Width        Image width in pixels
 *  @param Height       Image height in pixels
 *  @param Quality      JPEG quality (1-100, higher is better quality)
 *  @param Rotation     Clockwise rotation applied before encoding (JPEG_ROTATE_0D … JPEG_ROTATE_270D)
 *  @param p_Output     Pointer to store encoded JPEG data pointer
 *  @param p_Size       Pointer to store encoded data size
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if parameters are invalid
 *                      ESP_ERR_NO_MEM if memory allocation fails
 *                      ESP_FAIL if encoding fails
 */
esp_err_t JPEGEncoder_Encode(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                             uint8_t Quality, jpeg_rotate_t Rotation,
                             uint8_t **p_Output, size_t *p_Size);

#endif /* JPEG_ENCODER_H_ */
