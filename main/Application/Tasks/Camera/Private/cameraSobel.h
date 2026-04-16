/*
 * cameraSobel.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Sobel edge-detection filter for RGB565 camera frames.
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

#ifndef CAMERA_SOBEL_H_
#define CAMERA_SOBEL_H_

#include <stdint.h>

/** @defgroup CAMERA_SOBEL Camera Sobel Filter
 *  @brief Row-decomposed Sobel edge-detection filter for RGB565 frames.
 *
 *  The filter operates on big-endian RGB565 frames (as output by the camera DMA driver)
 *  and writes a grayscale edge-magnitude image in the same big-endian RGB565 format.
 *
 *  SIMD acceleration is achieved by decomposing the 2-D Sobel kernel into two
 *  separable row-wise operations that are computed using the esp-dsp
 *  @c dsps_add_s16 / @c dsps_sub_s16 primitives (ae32/aes3 SIMD back-end:
 *  4 or 8 int16 values per instruction cycle depending on the target):
 *
 *  Horizontal difference row (for Gx):
 *  @code
 *    dx[x] = gray[x+1] - gray[x-1]        (dsps_sub_s16, W-2 elements)
 *  @endcode
 *
 *  Horizontal smoothing row (for Gy):
 *  @code
 *    sx[x] = gray[x-1] + 2*gray[x] + gray[x+1]  (3 x dsps_add_s16, W-2 elements)
 *  @endcode
 *
 *  Output row (centre of 3-row window at row y):
 *  @code
 *    Gx[x] = dx[y-1][x] + 2*dx[y][x] + dx[y+1][x]   (3 x dsps_add_s16)
 *    Gy[x] = sx[y+1][x] - sx[y-1][x]                 (1 x dsps_sub_s16)
 *  @endcode
 *
 *  Edge magnitude: @c mag = |Gx| + |Gy|, clamped to [0, 255].
 *  @{
 */

/** @brief              Apply Sobel edge-detection to an RGB565 frame.
 *                      Converts each pixel to 8-bit grayscale using BT.601 luma coefficients
 *                      (approximation with integer arithmetic), then computes the Sobel
 *                      gradient magnitude using a row-decomposed algorithm accelerated by
 *                      the esp-dsp ae32/aes3 SIMD back-end.
 *                      The output is a grayscale edge image encoded as big-endian RGB565
 *                      (same byte layout as the camera driver output), written to
 *                      @p p_DstRGB565.  Top, bottom, left and right border pixels are
 *                      set to zero (black).
 *  @note               @p p_SrcRGB565 and @p p_DstRGB565 may point to different buffers
 *                      or to the same buffer (in-place is safe because output row (y-1)
 *                      is written only after rows y-1, y, and y+1 have all been read).
 *  @note               The function uses static intermediate buffers in internal DRAM
 *                      and is therefore NOT re-entrant / NOT thread-safe.
 *                      Call only from a single task context (e.g. the Camera Task).
 *  @note               Maximum supported frame width is 320 pixels.
 *  @warning            Width must be >= 3 and Height must be >= 3.
 *  @param p_SrcRGB565  Pointer to the source RGB565 frame buffer (big-endian, Width * Height * 2 bytes).
 *  @param p_DstRGB565  Pointer to the destination RGB565 frame buffer (Width * Height * 2 bytes).
 *  @param Width        Frame width in pixels (must be <= 320).
 *  @param Height       Frame height in pixels.
 */
void Camera_Sobel_ApplyFilter(const uint8_t *p_SrcRGB565, uint8_t *p_DstRGB565, uint32_t Width, uint32_t Height);

/** @} */

#endif /* CAMERA_SOBEL_H_ */
