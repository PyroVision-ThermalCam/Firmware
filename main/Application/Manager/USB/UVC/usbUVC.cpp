/*
 * usbUVC.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB UVC (Video Class) module implementation.
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
#include <esp_event.h>
#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string.h>

#include <sdkconfig.h>

#include "usbUVC.h"
#include "Application/Manager/USB/usbManager.h"

#include <tinyusb_uvc.h>

static const char *TAG = "USB-UVC";

/* Number of ping-pong buffers */
#define UVC_NUM_BUFFERS                         2

/* Maximum frame size for MJPEG (RGB frame can be compressed to ~10% of original) */
#define UVC_MAX_FRAME_SIZE                      (160 * 120 * 3)

/** @brief UVC module internal state.
 */
typedef struct {
    bool IsInitialized;                         /**< Module initialization state. */
    bool IsStreaming;                           /**< Host is actively streaming. */
    uint16_t Width;                             /**< Frame width in pixels. */
    uint16_t Height;                            /**< Frame height in pixels. */
    uint8_t FrameRate;                          /**< Target frame rate. */
    uint8_t CurrentWriteBuffer;                 /**< Index of current write buffer. */
    uint8_t CurrentReadBuffer;                  /**< Index of current read buffer. */
    USB_UVC_FrameBuffer_t Buffers[UVC_NUM_BUFFERS]; /**< Ping-pong frame buffers. */
    SemaphoreHandle_t BufferMutex;              /**< Mutex for buffer access. */
} USB_UVCState_t;

static USB_UVCState_t _UVCState;

/** @brief          Callback when streaming starts.
 *  @param itf      Interface number
 *  @param p_Event  Pointer to UVC event
 */
static void on_UVC_StreamingStart(int itf, uvc_event_t *p_Event)
{
    if (p_Event->type == UVC_EVENT_STREAMING_START) {
        ESP_LOGD(TAG, "UVC streaming started on interface %d", itf);

        _UVCState.IsStreaming = true;

        esp_err_t Error = esp_event_post(USB_EVENTS, USB_EVENT_UVC_STREAMING_START, NULL, 0, 0);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to post streaming start event: 0x%X", Error);
        }
    } else if (p_Event->type == UVC_EVENT_FRAME_END) {
        ESP_LOGD(TAG, "Frame transfer complete on interface %d", itf);
    }
}

/** @brief          Callback when streaming stops.
 *  @param itf      Interface number
 *  @param p_Event  Pointer to UVC event
 */
static void on_UVC_StreamingStop(int itf, uvc_event_t *p_Event)
{
    if (p_Event->type == UVC_EVENT_STREAMING_STOP) {
        ESP_LOGI(TAG, "UVC streaming stopped on interface %d", itf);

        _UVCState.IsStreaming = false;

        esp_err_t Error = esp_event_post(USB_EVENTS, USB_EVENT_UVC_STREAMING_STOP, NULL, 0, 0);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to post streaming stop event: 0x%X", Error);
        }
    }
}

/** @brief              Callback to request a frame buffer from the application.
 *  @param itf          Interface number
 *  @param pp_Buffer    Pointer to store frame buffer address
 *  @param p_BufferSize Pointer to store frame buffer size
 *  @return             true if buffer available, false otherwise
 */
static bool on_UVC_FrameRequest(int itf, uint8_t **pp_Buffer, size_t *p_BufferSize)
{
    if (xSemaphoreTake(_UVCState.BufferMutex, 0) == pdFALSE) {
        return false;
    }

    /* Check if read buffer has valid data */
    uint8_t ReadIdx = _UVCState.CurrentReadBuffer;
    if (_UVCState.Buffers[ReadIdx].IsReady == false) {
        xSemaphoreGive(_UVCState.BufferMutex);

        return false;
    }

    *pp_Buffer = _UVCState.Buffers[ReadIdx].p_Buffer;
    *p_BufferSize = _UVCState.Buffers[ReadIdx].Size;

    ESP_LOGD(TAG, "Frame request: buffer[%d], size=%d", ReadIdx, _UVCState.Buffers[ReadIdx].Size);

    xSemaphoreGive(_UVCState.BufferMutex);

    return true;
}

/** @brief          Callback to return a frame buffer after transmission.
 *  @param itf      Interface number
 *  @param p_Buffer Frame buffer address
 */
static void on_UVC_FrameReturn(int itf, uint8_t *p_Buffer)
{
    if (xSemaphoreTake(_UVCState.BufferMutex, 0) == pdFALSE) {
        return;
    }

    /* Find and mark buffer as available */
    for (int i = 0; i < UVC_NUM_BUFFERS; i++) {
        if (_UVCState.Buffers[i].p_Buffer == p_Buffer) {
            _UVCState.Buffers[i].IsReady = false;

            /* Advance read buffer to next */
            _UVCState.CurrentReadBuffer = (i + 1) % UVC_NUM_BUFFERS;

            ESP_LOGD(TAG, "Frame returned: buffer[%d]", i);

            break;
        }
    }

    xSemaphoreGive(_UVCState.BufferMutex);
}

esp_err_t USBUVC_Init(const USB_UVC_Config_t *p_Config)
{
    esp_err_t Error;
    uint32_t Caps;

    if (p_Config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration!");

        return ESP_ERR_INVALID_ARG;
    } else if (_UVCState.IsInitialized) {
        ESP_LOGW(TAG, "UVC already initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    memset(&_UVCState, 0, sizeof(USB_UVCState_t));

    _UVCState.Width = p_Config->Width;
    _UVCState.Height = p_Config->Height;
    _UVCState.FrameRate = p_Config->FrameRate;

    /* Create mutex for buffer protection */
    _UVCState.BufferMutex = xSemaphoreCreateMutex();
    if (_UVCState.BufferMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex!");

        return ESP_ERR_NO_MEM;
    }

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM;
#else
    Caps = 0;
#endif

    for (int i = 0; i < UVC_NUM_BUFFERS; i++) {
        _UVCState.Buffers[i].p_Buffer = static_cast<uint8_t *>(heap_caps_malloc(UVC_MAX_FRAME_SIZE, Caps));
        if (_UVCState.Buffers[i].p_Buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer %d!", i);

            /* Cleanup previously allocated buffers */
            for (int j = 0; j < i; j++) {
                heap_caps_free(_UVCState.Buffers[j].p_Buffer);
                _UVCState.Buffers[j].p_Buffer = NULL;
            }

            vSemaphoreDelete(_UVCState.BufferMutex);
            _UVCState.BufferMutex = NULL;

            return ESP_ERR_NO_MEM;
        }

        _UVCState.Buffers[i].Size = 0;
        _UVCState.Buffers[i].IsReady = false;

        ESP_LOGD(TAG, "Allocated buffer[%d] at %p, size=%d", i, _UVCState.Buffers[i].p_Buffer, UVC_MAX_FRAME_SIZE);
    }

    tinyusb_config_uvc_t UVC_Config = {
        .uvc_port = TINYUSB_UVC_ITF_0,
        .callback_streaming_start = on_UVC_StreamingStart,
        .callback_streaming_stop = on_UVC_StreamingStop,
        .fb_request_cb = on_UVC_FrameRequest,
        .fb_return_cb = on_UVC_FrameReturn,
        .stop_cb = NULL,
        .uvc_buffer = NULL,
        .uvc_buffer_size = 0,
    };

    Error = tinyusb_uvc_init(&UVC_Config);

    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TinyUSB UVC: 0x%X!", Error);

        for (uint8_t i = 0; i < UVC_NUM_BUFFERS; i++) {
            heap_caps_free(_UVCState.Buffers[i].p_Buffer);
            _UVCState.Buffers[i].p_Buffer = NULL;
        }

        vSemaphoreDelete(_UVCState.BufferMutex);
        _UVCState.BufferMutex = NULL;

        return Error;
    }

    _UVCState.IsInitialized = true;

    ESP_LOGI(TAG, "UVC initialized: %dx%d @ %d fps", _UVCState.Width, _UVCState.Height, _UVCState.FrameRate);

    return ESP_OK;
}

esp_err_t USBUVC_Deinit(void)
{
    bool WasStreaming = false;

    if (_UVCState.IsInitialized == false) {
        ESP_LOGW(TAG, "UVC not initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Remember if we were streaming before deinit */
    WasStreaming = _UVCState.IsStreaming;

    tinyusb_uvc_deinit(TINYUSB_UVC_ITF_0);

    for (uint8_t i = 0; i < UVC_NUM_BUFFERS; i++) {
        if (_UVCState.Buffers[i].p_Buffer != NULL) {
            heap_caps_free(_UVCState.Buffers[i].p_Buffer);
            _UVCState.Buffers[i].p_Buffer = NULL;
        }
    }

    if (_UVCState.BufferMutex != NULL) {
        vSemaphoreDelete(_UVCState.BufferMutex);
        _UVCState.BufferMutex = NULL;
    }

    memset(&_UVCState, 0, sizeof(USB_UVCState_t));

    /* If we were streaming, post STOP event so tasks can clean up */
    if (WasStreaming) {
        esp_event_post(USB_EVENTS, USB_EVENT_UVC_STREAMING_STOP, NULL, 0, 0);
    }

    ESP_LOGI(TAG, "UVC deinitialized");

    return ESP_OK;
}

esp_err_t USBUVC_SubmitFrame(const uint8_t *p_Data, size_t Size)
{
    if (p_Data == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (Size == 0) {
        return ESP_ERR_INVALID_ARG;
    } else if (_UVCState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_UVCState.IsStreaming == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (Size > UVC_MAX_FRAME_SIZE) {
        ESP_LOGW(TAG, "Frame size %d exceeds maximum %d!", Size, UVC_MAX_FRAME_SIZE);

        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(_UVCState.BufferMutex, pdMS_TO_TICKS(10)) == pdFALSE) {
        ESP_LOGW(TAG, "Failed to acquire buffer mutex!");

        return ESP_ERR_TIMEOUT;
    }

    /* Find next available write buffer */
    uint8_t WriteIdx = _UVCState.CurrentWriteBuffer;

    /* If buffer is still in use, skip this frame */
    if (_UVCState.Buffers[WriteIdx].IsReady) {
        xSemaphoreGive(_UVCState.BufferMutex);

        ESP_LOGD(TAG, "Buffer[%d] still in use, dropping frame", WriteIdx);

        return ESP_ERR_NO_MEM;
    }

    /* Copy frame data to buffer */
    memcpy(_UVCState.Buffers[WriteIdx].p_Buffer, p_Data, Size);
    _UVCState.Buffers[WriteIdx].Size = Size;
    _UVCState.Buffers[WriteIdx].IsReady = true;

    /* Advance write buffer index */
    _UVCState.CurrentWriteBuffer = (WriteIdx + 1) % UVC_NUM_BUFFERS;

    /* Update read buffer if it hasn't been set */
    if (_UVCState.Buffers[_UVCState.CurrentReadBuffer].IsReady == false) {
        _UVCState.CurrentReadBuffer = WriteIdx;
    }

    ESP_LOGD(TAG, "Frame submitted: buffer[%d], size=%d", WriteIdx, Size);

    xSemaphoreGive(_UVCState.BufferMutex);

    return ESP_OK;
}

bool USBUVC_IsStreaming(void)
{
    return _UVCState.IsInitialized && _UVCState.IsStreaming;
}

bool USBUVC_IsInitialized(void)
{
    return _UVCState.IsInitialized;
}
