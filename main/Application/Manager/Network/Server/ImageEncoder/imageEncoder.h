/*
 * image_encoder.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Image encoder for thermal frames.
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

#ifndef IMAGE_ENCODER_H_
#define IMAGE_ENCODER_H_

#include <esp_err.h>

#include "../../networkTypes.h"
#include "Settings/settingsTypes.h"

/** @brief          Initialize the image encoder.
 *  @param Quality  JPEG quality (1-100)
 *  @return         ESP_OK on success
 */
esp_err_t ImageEncoder_Init(uint8_t Quality);

/** @brief Deinitialize the image encoder.
 */
void ImageEncoder_Deinit(void);

/** @brief              Encode a thermal frame to the specified format.
 *  @param p_Frame      Pointer to thermal frame data
 *  @param Format       Output image format
 *  @param Palette      Color palette to use
 *  @param p_Encoded    Pointer to store encoded image data
 *  @return             ESP_OK on success
 */
esp_err_t ImageEncoder_Encode(const Network_Thermal_Frame_t *p_Frame,
                              Settings_Image_Format_t Format,
                              Server_Palette_t Palette,
                              Network_Encoded_Image_t *p_Encoded);

/** @brief              Free encoded image data.
 *  @param p_Encoded    Pointer to encoded image structure
 */
void ImageEncoder_Free(Network_Encoded_Image_t *p_Encoded);

/** @brief          Set JPEG encoding quality.
 *  @param Quality  Quality value (1-100)
 */
void ImageEncoder_SetQuality(uint8_t Quality);

#endif /* IMAGE_ENCODER_H_ */
