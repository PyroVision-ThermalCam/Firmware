/*
 * cameraSobel.cpp
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <esp_attr.h>

#include <dsps_add.h>
#include <dsps_sub.h>

#include "cameraSobel.h"

/* Maximum supported frame width.  Matches FRAMESIZE_QVGA (320). */
#define SOBEL_MAX_WIDTH         320

/* Number of valid inner pixels per row (1-pixel border excluded on each side). */
#define SOBEL_INNER_W           (SOBEL_MAX_WIDTH - 2)

/* Static intermediate buffers in internal DRAM.
 *
 * Using DRAM_ATTR ensures placement in data RAM even when IRAM is configured
 * as cache.  Keeping these in internal RAM (not PSRAM) is critical for
 * performance: the SIMD dsps_add_s16 / dsps_sub_s16 ae32 back-end loads
 * 4 int16 values per instruction and would stall on PSRAM latency.
 *
 * Memory budget (all in internal DRAM):
 *   _Gray : 3 x 320 x 2 = 1 920 bytes
 *   _Dx   : 3 x 318 x 2 = 1 908 bytes   (horizontal difference  dx[x] = g[x+1]-g[x-1])
 *   _Sx   : 3 x 318 x 2 = 1 908 bytes   (horizontal smoothed    sx[x] = g[x-1]+2g[x]+g[x+1])
 *   _Gx   :     318 x 2 =   636 bytes   (Sobel X gradient for one output row)
 *   _Gy   :     318 x 2 =   636 bytes   (Sobel Y gradient for one output row)
 *                        --------
 *                         7 008 bytes total
 */
DRAM_ATTR static int16_t _Gray[3][SOBEL_MAX_WIDTH];
DRAM_ATTR static int16_t _Dx[3][SOBEL_INNER_W];
DRAM_ATTR static int16_t _Sx[3][SOBEL_INNER_W];
DRAM_ATTR static int16_t _Gx[SOBEL_INNER_W];
DRAM_ATTR static int16_t _Gy[SOBEL_INNER_W];

/** @brief Convert one row of big-endian RGB565 pixels to int16_t grayscale.
 *
 * Camera DMA output is big-endian RGB565:
 *   buf[2i+0] = high byte (RRRRR GGG)
 *   buf[2i+1] = low byte  (GGG BBBBB)
 *
 * Reading as uint16_t on the little-endian ESP32 yields the byte-swapped value.
 * __builtin_bswap16() recovers the canonical RGB565 bit assignment:
 *   bit 15..11 = R5, bit 10..5 = G6, bit 4..0 = B5
 *
 * Grayscale via BT.601 (integer approximation, no true 8-bit expansion):
 *   R8 ≈ R5 << 3,  G8 ≈ G6 << 2,  B8 ≈ B5 << 3
 *   Y8 = (77*R8 + 150*G8 + 29*B8) >> 8
 *      = (616*R5 + 600*G6 + 232*B5) >> 8
 *   Output range: [0, ~250] → stored as int16_t (no overflow).
 *
 * Two pixels are extracted per loop iteration from a single uint32_t load
 * to exploit the 32-bit data bus and reduce loop overhead by 2x.
 */
static void Sobel_ConvertRow(const uint8_t *p_Src, int16_t *p_Gray, uint32_t Width)
{
    const uint32_t *p_Src32 = reinterpret_cast<const uint32_t *>(p_Src);
    uint32_t i = 0;

    /* Process two pixels per loop iteration (one 32-bit load). */
    for (; i + 1 < Width; i += 2) {
        uint32_t Two = *p_Src32++;

        /* Pixel 0 occupies the low 16 bits of the little-endian uint32_t.
         * On the LE host: low16 = buf[0] | (buf[1]<<8).
         * Byte-swap gives the canonical big-endian RGB565 value. */
        uint32_t Px0 = static_cast<uint32_t>(__builtin_bswap16(static_cast<uint16_t>(Two & 0xFFFFu)));
        uint32_t Px1 = static_cast<uint32_t>(__builtin_bswap16(static_cast<uint16_t>(Two >> 16u)));

        p_Gray[i]     = static_cast<int16_t>((((Px0 >> 11u) & 0x1Fu) * 616u +
                                               ((Px0 >>  5u) & 0x3Fu) * 600u +
                                               ( Px0         & 0x1Fu) * 232u) >> 8u);

        p_Gray[i + 1] = static_cast<int16_t>((((Px1 >> 11u) & 0x1Fu) * 616u +
                                               ((Px1 >>  5u) & 0x3Fu) * 600u +
                                               ( Px1         & 0x1Fu) * 232u) >> 8u);
    }

    /* Handle odd frame width (rare, but defensive). */
    if (i < Width) {
        const uint16_t *p_Last = reinterpret_cast<const uint16_t *>(p_Src + i * 2u);
        uint32_t Px = static_cast<uint32_t>(__builtin_bswap16(*p_Last));

        p_Gray[i] = static_cast<int16_t>((((Px >> 11u) & 0x1Fu) * 616u +
                                           ((Px >>  5u) & 0x3Fu) * 600u +
                                           ( Px         & 0x1Fu) * 232u) >> 8u);
    }
}

/** @brief Compute the horizontal-difference and horizontal-smoothing row vectors
 *        for the given grayscale row.  Both output arrays have Width-2 elements
 *        (indices 0..Width-3) corresponding to source pixel columns 1..Width-2.
 *
 * Definitions (x = 0..Width-3 of the output, i.e. source column x+1):
 *   dx[x] = gray[x+2] - gray[x]          (= gray[col+1] - gray[col-1])
 *   sx[x] = gray[x]  + gray[x+2]          outer sum
 *         + gray[x+1] + gray[x+1]          + 2 * centre
 *
 * All four operations are performed as row-level vector additions / subtractions
 * using the dsps_add_s16 / dsps_sub_s16 primitives.  With CONFIG_DSP_OPTIMIZED
 * these resolve to dsps_add_s16_aes3 (8 int16/cycle) or dsps_add_s16_ae32
 * (4 int16/cycle) depending on the chip variant.
 *
 * Overflow analysis:
 *   gray range  : [0, 250]
 *   dx range    : [-250, 250]   → fits in int16_t ✓
 *   sx (outer)  : [0, 500]      → fits in int16_t ✓
 *   sx (+ cx2)  : [0, 1 000]    → fits in int16_t ✓
 */
static void Sopel_PrepareRow(const int16_t *p_Gray, int16_t *p_Dx, int16_t *p_Sx, uint32_t Width)
{
    int InnerLen = static_cast<int>(Width) - 2;

    /* dx[x] = gray[x+2] - gray[x]  (pointers offset by 2 and 0 into p_Gray) */
    dsps_sub_s16(p_Gray + 2, p_Gray + 0, p_Dx, InnerLen, 1, 1, 1, 0);

    /* sx[x] = gray[x] + gray[x+2]  (outer neighbours) */
    dsps_add_s16(p_Gray + 0, p_Gray + 2, p_Sx, InnerLen, 1, 1, 1, 0);

    /* sx[x] += gray[x+1]  (centre, first time) */
    dsps_add_s16(p_Sx, p_Gray + 1, p_Sx, InnerLen, 1, 1, 1, 0);

    /* sx[x] += gray[x+1]  (centre, second time  →  total: 2 * gray[x+1]) */
    dsps_add_s16(p_Sx, p_Gray + 1, p_Sx, InnerLen, 1, 1, 1, 0);
}

/** @brief Encode a single-channel uint8_t value as a big-endian grayscale RGB565 word.
 *
 * Grayscale RGB565:
 *   R5 = Y >> 3,  G6 = Y >> 2,  B5 = Y >> 3
 *   rgb565 = (R5 << 11) | (G6 << 5) | B5
 *
 * Stored in big-endian byte order (matching camera DMA output) using bswap16.
 */
static inline uint16_t sobel_gray8_to_rgb565be(uint8_t Y)
{
    uint16_t Rgb565 = static_cast<uint16_t>(
                          (static_cast<uint16_t>(Y >> 3u) << 11u) |
                          (static_cast<uint16_t>(Y >> 2u) <<  5u) |
                          (static_cast<uint16_t>(Y >> 3u)));

    return __builtin_bswap16(Rgb565);
}

void Camera_Sobel_ApplyFilter(const uint8_t *p_SrcRGB565, uint8_t *p_DstRGB565,
                              uint32_t Width, uint32_t Height)
{
    int InnerLen = static_cast<int>(Width) - 2;

    /* Pre-load the first two source rows into the ring buffer */
    for (uint32_t y = 0; y < 2u; y++) {
        const uint8_t *p_SrcRow = p_SrcRGB565 + y * Width * 2u;

        Sobel_ConvertRow(p_SrcRow, _Gray[y % 3u], Width);
        Sopel_PrepareRow(_Gray[y % 3u], _Dx[y % 3u], _Sx[y % 3u], Width);
    }

    /* ---------- top border row: output all zeros ---------- */
    memset(p_DstRGB565, 0, Width * 2u);

    /* ---------- main sliding-window loop ---------- */
    for (uint32_t y = 1u; y < Height - 1u; y++) {
        /* Load the row that forms the bottom of the current 3-row window. */
        uint32_t NextRow = y + 1u;
        const uint8_t *p_SrcRow = p_SrcRGB565 + NextRow * Width * 2u;
        uint32_t RingNext = NextRow % 3u;

        Sobel_ConvertRow(p_SrcRow, _Gray[RingNext], Width);
        Sopel_PrepareRow(_Gray[RingNext], _Dx[RingNext], _Sx[RingNext], Width);

        /* Ring indices for the three window rows. */
        uint32_t RingPrev = (y - 1u) % 3u;
        uint32_t RingCurr =  y       % 3u;
        /* RingNext already set above. */

        /* Gx = dx[y-1] + 2*dx[y] + dx[y+1]
         *
         * Built with three SIMD add passes (each processes InnerLen int16 values
         * using 4 or 8 values per instruction cycle):
         *   pass 1: Gx = dx[y-1] + dx[y+1]
         *   pass 2: Gx += dx[y]
         *   pass 3: Gx += dx[y]          (total centre contribution = 2 * dx[y])
         */
        dsps_add_s16(_Dx[RingPrev], _Dx[RingNext], _Gx, InnerLen, 1, 1, 1, 0);
        dsps_add_s16(_Gx,           _Dx[RingCurr], _Gx, InnerLen, 1, 1, 1, 0);
        dsps_add_s16(_Gx,           _Dx[RingCurr], _Gx, InnerLen, 1, 1, 1, 0);

        /* Gy = sx[y+1] - sx[y-1]
         *
         * Single SIMD subtract pass (the horizontal smoothing already encodes the
         * row-wise Gaussian weights, so vertical differencing is all that remains).
         */
        dsps_sub_s16(_Sx[RingNext], _Sx[RingPrev], _Gy, InnerLen, 1, 1, 1, 0);

        /* Encode output row y:
         *   - column 0           : zero (left border)
         *   - columns 1..Width-2 : edge magnitude = clamp(|Gx| + |Gy|, 0, 255)
         *   - column Width-1     : zero (right border)
         */
        uint16_t *p_DstRow = reinterpret_cast<uint16_t *>(p_DstRGB565 + y * Width * 2u);

        p_DstRow[0] = 0u;

        for (int x = 0; x < InnerLen; x++) {
            int32_t Mag = abs(static_cast<int32_t>(_Gx[x])) +
                          abs(static_cast<int32_t>(_Gy[x]));

            if (Mag > 255) {
                Mag = 255;
            }

            p_DstRow[x + 1] = sobel_gray8_to_rgb565be(static_cast<uint8_t>(Mag));
        }

        p_DstRow[Width - 1u] = 0u;
    }

    /* ---------- bottom border row: output all zeros ---------- */
    memset(p_DstRGB565 + (Height - 1u) * Width * 2u, 0, Width * 2u);
}
