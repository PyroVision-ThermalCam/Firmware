/*
 * usbUVC.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB UVC (Video Class) module definition.
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

#ifndef USB_UVC_H_
#define USB_UVC_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>

/** @brief UVC frame buffer structure for ping-pong buffering.
 */
typedef struct {
    uint8_t *p_Buffer;                      /**< Pointer to frame data. */
    size_t Size;                            /**< Size of frame data in bytes. */
    bool isReady;                           /**< Buffer contains valid frame data. */
} USB_UVC_FrameBuffer_t;

/** @brief UVC configuration structure.
 */
typedef struct {
    uint16_t Width;                         /**< Frame width in pixels. */
    uint16_t Height;                        /**< Frame height in pixels. */
    uint8_t FrameRate;                      /**< Target frame rate in fps. */
} USB_UVC_Config_t;

/** @brief          Initialize the USB UVC module.
 *                  Sets up ping-pong frame buffers and UVC streaming infrastructure.
 *  @note           Must be called after TinyUSB driver is installed.
 *                  Requires CONFIG_TINYUSB_UVC_ENABLED=y in sdkconfig.
 *  @warning        Frame buffers are allocated in PSRAM for performance.
 *  @param p_Config Pointer to UVC configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL
 *                  ESP_ERR_NO_MEM if buffer allocation fails
 *                  ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t USBUVC_Init(const USB_UVC_Config_t *p_Config);

/** @brief          Deinitialize the USB UVC module.
 *                  Stops streaming and frees all allocated buffers.
 *  @note           Safe to call even if not initialized.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBUVC_Deinit(void);

/** @brief          Submit a frame for USB streaming.
 *                  Copies frame data to internal ping-pong buffer and queues for transmission.
 *  @note           Non-blocking. If previous frame not transmitted, this frame is dropped.
 *                  Frame data must be in JPEG format for UVC MJPEG mode.
 *  @warning        Do not call from ISR context.
 *  @param p_Data   Pointer to frame data buffer
 *  @param Size     Size of frame data in bytes
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Data is NULL or Size is 0
 *                  ESP_ERR_INVALID_STATE if not initialized or not streaming
 *                  ESP_ERR_NO_MEM if no buffer available
 */
esp_err_t USBUVC_SubmitFrame(const uint8_t *p_Data, size_t Size);

/** @brief          Check if UVC streaming is currently active.
 *                  Returns true when host has started video streaming.
 *  @note           Thread-safe.
 *  @return         true if USB host is actively streaming
 *                  false if not streaming or not initialized
 */
bool USBUVC_IsStreaming(void);

/** @brief          Check if UVC module is initialized.
 *  @note           Thread-safe.
 *  @return         true if module is initialized
 *                  false if not initialized
 */
bool USBUVC_IsInitialized(void);

#endif /* USB_UVC_H_ */
