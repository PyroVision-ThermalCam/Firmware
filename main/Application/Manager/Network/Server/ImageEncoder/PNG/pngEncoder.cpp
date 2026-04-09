/*
 * pngEncoder.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PNG encoder implementation.
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

#include "pngEncoder.h"

static const char *TAG = "PNG-Encoder";

esp_err_t PNGEncoder_Encode(const uint8_t *p_RGB, uint16_t Width, uint16_t Height,
                            uint8_t **p_Output, size_t *p_Size)
{
    if ((p_RGB == NULL) || (p_Output == NULL) || (p_Size == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if ((Width == 0) || (Height == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* TODO: Implement PNG encoding using a library such as:
     *       - lodepng (https://github.com/lvandeve/lodepng) - Simple, single-file
     *       - libpng (http://www.libpng.org/) - Full-featured, standard library
     *       - stb_image_write (https://github.com/nothings/stb) - Single-header library
     *
     * Example integration with lodepng:
     *
     * #include "lodepng.h"
     *
     * unsigned char *png_data;
     * size_t png_size;
     * unsigned error = lodepng_encode24(&png_data, &png_size, p_RGB, Width, Height);
     *
     * if (error) {
     *     ESP_LOGE(TAG, "PNG encoding error %u: %s", error, lodepng_error_text(error));
     *     return ESP_FAIL;
     * }
     *
     * // Allocate output buffer with proper memory caps
     * *p_Output = static_cast<uint8_t *>(heap_caps_malloc(png_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
     * if (*p_Output == NULL) {
     *     free(png_data);
     *     return ESP_ERR_NO_MEM;
     * }
     *
     * memcpy(*p_Output, png_data, png_size);
     * free(png_data);
     * *p_Size = png_size;
     *
     * return ESP_OK;
     */

    ESP_LOGW(TAG, "PNG encoding is not yet implemented!");
    ESP_LOGW(TAG, "Please integrate a PNG library (lodepng, libpng, or stb_image_write)");

    return ESP_ERR_NOT_SUPPORTED;
}
