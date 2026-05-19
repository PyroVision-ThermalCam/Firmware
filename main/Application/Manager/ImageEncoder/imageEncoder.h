/*
 * imageEncoder.h
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

#ifndef IMAGE_ENCODER_H_
#define IMAGE_ENCODER_H_

#include <esp_err.h>

#include "../Network/networkTypes.h"
#include "../Settings/settingsTypes.h"
#include "imageEncoderTypes.h"

/** @defgroup IMAGE_ENCODER_ERRORS Image Encoder Error Codes
 *  @brief Error codes returned by ImageEncoder functions (base: @c IMAGE_ENCODER_ERR_BASE = 0x7000).
 *  @{
 */

#define IMAGE_ENCODER_ERR_BASE              0x7000                                  /**< Image Encoder error base. */
#define IMAGE_ENCODER_ERR_NOT_INITIALIZED   (IMAGE_ENCODER_ERR_BASE + 0x01)        /**< Module not yet initialized. */
#define IMAGE_ENCODER_ERR_INVALID_ARG       (IMAGE_ENCODER_ERR_BASE + 0x08)        /**< NULL pointer or out-of-range parameter. */
#define IMAGE_ENCODER_ERR_INVALID_STATE     (IMAGE_ENCODER_ERR_BASE + 0x09)        /**< Precondition not met. */

/** @} */

/** @brief          Initialize the image encoder.
 *  @return         ESP_OK on success
 */
esp_err_t ImageEncoder_Init(void);

/** @brief Deinitialize the image encoder.
 */
void ImageEncoder_Deinit(void);

/** @brief              Encode a thermal frame to the specified format.
 *                      Always encodes the pre-rendered RGB888 display buffer (p_Frame->Buffer),
 *                      which reflects the current on-screen content regardless of the active
 *                      palette or view mode (thermal or RGB camera).
 *  @note               No palette re-application is performed; the caller is responsible for
 *                      keeping p_Frame->Buffer up-to-date with the latest rendered frame.
 *  @param p_Frame      Pointer to frame data; Buffer must not be NULL
 *  @param Format       Output image format
 *  @param p_Encoded    Pointer to store encoded image data
 *  @param JpegQuality  JPEG quality (1-100); only used if Format is IMAGE_FORMAT_JPEG
 *  @return             ESP_OK on success
 *                      IMAGE_ENCODER_ERR_INVALID_ARG if p_Frame, p_Frame->Buffer, or p_Encoded is NULL
 *                      IMAGE_ENCODER_ERR_NOT_INITIALIZED if the encoder has not been initialized
 *                      ESP_ERR_NO_MEM if buffer allocation fails
 *                      ESP_ERR_NOT_SUPPORTED if the requested format encoder is disabled at build time
 */
esp_err_t ImageEncoder_Encode(const ImageEncoder_Raw_t *p_Frame,
                              ImageEncoder_Format_t Format,
                              ImageEncoder_EncodedImage_t *p_Encoded,
                              uint8_t JpegQuality);

/** @brief              Free encoded image data.
 *  @param p_Encoded    Pointer to encoded image structure
 */
void ImageEncoder_Free(ImageEncoder_EncodedImage_t *p_Encoded);

#ifdef CONFIG_IMAGE_ENCODER_JPEG
/** @brief          Set JPEG encoding quality.
 *  @param Quality  Quality value (1-100)
 */
void ImageEncoder_SetQuality(uint8_t Quality);
#endif

#endif /* IMAGE_ENCODER_H_ */
