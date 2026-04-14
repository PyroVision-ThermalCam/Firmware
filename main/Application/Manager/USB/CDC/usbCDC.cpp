/*
 * usbCDC.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB CDC-ACM module implementation.
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

#include <string.h>

#include <sdkconfig.h>

#include "usbCDC.h"
#include "Application/Manager/USB/usbManager.h"

#include <tinyusb_cdc_acm.h>

static const char *TAG = "USB-CDC";

/** @brief CDC module internal state.
 */
typedef struct {
    bool IsInitialized;                 /**< Module initialization state. */
    bool IsConnected;                   /**< Host terminal is connected (DTR+RTS active). */
} USB_CDCState_t;

static USB_CDCState_t _CDCState;

/** @brief              Callback for CDC line state changes (connect / disconnect events).
 *  @param itf          Interface number
 *  @param p_Event      Pointer to CDC event
 */
static void on_CDC_LineStateChanged(int itf, cdcacm_event_t *p_Event)
{
    esp_err_t Error;
    bool Connected = p_Event->line_state_changed_data.dtr && p_Event->line_state_changed_data.rts;

    if (Connected == _CDCState.IsConnected) {
        return;
    }

    _CDCState.IsConnected = Connected;

    if (Connected) {
        ESP_LOGD(TAG, "CDC host terminal connected on interface %d", itf);

        Error = esp_event_post(USB_EVENTS, USB_EVENT_CDC_CONNECTED, NULL, 0, pdMS_TO_TICKS(100));
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to post CDC connected event: 0x%X", Error);
        }
    } else {
        ESP_LOGD(TAG, "CDC host terminal disconnected on interface %d", itf);

        Error = esp_event_post(USB_EVENTS, USB_EVENT_CDC_DISCONNECTED, NULL, 0, pdMS_TO_TICKS(100));
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to post CDC disconnected event: 0x%X", Error);
        }
    }
}

esp_err_t USBCDC_Init(const USB_CDC_Config_t *p_Config)
{
    esp_err_t Error;

    if (p_Config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration pointer!");

        return ESP_ERR_INVALID_ARG;
    } else if (_CDCState.IsInitialized) {
        ESP_LOGW(TAG, "CDC already initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    memset(&_CDCState, 0, sizeof(USB_CDCState_t));

    tinyusb_config_cdcacm_t CDC_Config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = on_CDC_LineStateChanged,
        .callback_line_coding_changed = NULL,
    };

    Error = tinyusb_cdcacm_init(&CDC_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TinyUSB CDC-ACM: 0x%X!", Error);

        return Error;
    }

    _CDCState.IsInitialized = true;

    ESP_LOGD(TAG, "CDC-ACM initialized");

    return ESP_OK;
}

esp_err_t USBCDC_Deinit(void)
{
    esp_err_t Error;

    if (_CDCState.IsInitialized == false) {
        ESP_LOGW(TAG, "CDC not initialized!");

        return ESP_ERR_INVALID_STATE;
    }

    Error = tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize TinyUSB CDC-ACM: 0x%X!", Error);
    }

    memset(&_CDCState, 0, sizeof(USB_CDCState_t));

    ESP_LOGD(TAG, "CDC-ACM deinitialized");

    return ESP_OK;
}

esp_err_t USBCDC_Write(const uint8_t *p_Data, size_t Size)
{
    esp_err_t Error;

    if (p_Data == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (Size == 0) {
        return ESP_ERR_INVALID_ARG;
    } else if (_CDCState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_CDCState.IsConnected == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, p_Data, Size) == 0) {
        ESP_LOGW(TAG, "Write queue full - data dropped!");

        return ESP_FAIL;
    }

    /* Non-blocking flush (timeout = 0) */
    Error = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    if (Error != ESP_OK && Error != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "CDC flush failed: 0x%X", Error);
    }

    return ESP_OK;
}

bool USBCDC_IsInitialized(void)
{
    return _CDCState.IsInitialized;
}

bool USBCDC_IsConnected(void)
{
    return _CDCState.IsInitialized && _CDCState.IsConnected;
}
