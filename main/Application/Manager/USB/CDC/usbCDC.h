/*
 * usbCDC.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: USB CDC-ACM module definition.
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

#ifndef USB_CDC_H_
#define USB_CDC_H_

#include "usbCDCTypes.h"

/** @brief          Initialize the USB CDC-ACM module.
 *                  Registers line-state and receive callbacks with TinyUSB CDC-ACM
 *                  and enables the virtual serial port on the host.
 *  @note           Must be called after TinyUSB driver is installed.
 *                  Requires CONFIG_TINYUSB_CDC_ENABLED=y in sdkconfig.
 *                  Posts USB_EVENT_CDC_CONNECTED / USB_EVENT_CDC_DISCONNECTED events
 *                  when the host terminal connects or disconnects.
 *  @warning        Not thread-safe. Call once during USB initialization.
 *  @param p_Config Pointer to CDC configuration structure
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Config is NULL
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if TinyUSB CDC-ACM initialization fails
 */
esp_err_t USBCDC_Init(const USB_CDC_Config_t *p_Config);

/** @brief      Deinitialize the USB CDC-ACM module.
 *              Unregisters all callbacks and frees internal state.
 *  @note       Safe to call even if not streaming.
 *  @return     ESP_OK on success
 *              ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t USBCDC_Deinit(void);

/** @brief          Write data to the CDC transmit buffer and flush to the host.
 *                  Data is queued in TinyUSB's internal write buffer and immediately flushed.
 *  @note           Non-blocking on flush (timeout = 0). Drops data if host is not connected.
 *                  This function is NOT safe to call from ISR context.
 *  @param p_Data   Pointer to data buffer to transmit
 *  @param Size     Number of bytes to transmit
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_ARG if p_Data is NULL or Size is 0
 *                  ESP_ERR_INVALID_STATE if not initialized or host not connected
 */
esp_err_t USBCDC_Write(const uint8_t *p_Data, size_t Size);

/** @brief  Check whether the CDC module is initialized.
 *  @return true if initialized
 *          false if not initialized
 */
bool USBCDC_IsInitialized(void);

/** @brief  Check whether a host terminal is currently connected (DTR+RTS set).
 *  @note   Thread-safe.
 *  @return true if host terminal is connected
 *          false if not connected or not initialized
 */
bool USBCDC_IsConnected(void);

#endif /* USB_CDC_H_ */
