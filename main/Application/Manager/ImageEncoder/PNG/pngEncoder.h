/*
 * pngEncoder.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PNG encoder for thermal images.
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

#ifndef PNG_ENCODER_H_
#define PNG_ENCODER_H_

#include <esp_err.h>

#include <stdint.h>
#include <stddef.h>

/** @brief          Encode RGB888 data to PNG format.
 *  @note           PNG encoding requires an external library (e.g., lodepng, libpng).
 *                  This is currently a placeholder implementation.
 *                  TODO: Integrate PNG encoding library for full PNG support.
 *  @param p_RGB    Pointer to RGB888 pixel data (width * height * 3 bytes)
 *  @param Width    Image width in pixels
 *  @param Height   Image height in pixels
 *  @param p_Output Pointer to store encoded PNG data pointer
 *  @param p_Size   Pointer to store encoded data size
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if parameters are invalid
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_ERR_NOT_SUPPORTED if PNG encoding is not implemented
 *                  ESP_FAIL if encoding fails
 */
esp_err_t PNGEncoder_Encode(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                            uint8_t **p_Output, size_t *p_Size);

#endif /* PNG_ENCODER_H_ */
