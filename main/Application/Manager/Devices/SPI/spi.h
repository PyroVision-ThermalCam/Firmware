/*
 * spi.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Central SPI bus manager for shared SPI peripherals.
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

#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>
#include <stdbool.h>

#include <driver/spi_master.h>
#include <driver/spi_common.h>

#include <esp_err.h>

/** @brief              Initialize the SPI bus.
 *  @param p_Config     Pointer to SPI bus configuration
 *  @param Host         SPI host device (SPI2_HOST or SPI3_HOST)
 *  @param DMA_Channel  DMA channel (SPI_DMA_CH_AUTO recommended)
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_STATE if bus already initialized
 */
esp_err_t SPIM_Init(const spi_bus_config_t *p_Config, spi_host_device_t Host, int DMA_Channel);

/** @brief              Deinitialize the SPI bus.
 *  @param Host         SPI host device to deinitialize
 *  @return             ESP_OK when successful
 */
esp_err_t SPIM_Deinit(spi_host_device_t Host);

/** @brief              Add a device to the SPI bus.
 *  @param Host         SPI host device
 *  @param p_Dev_Config Device configuration
 *  @param p_Handle     Pointer to store device handle
 *  @return             ESP_OK when successful
 */
esp_err_t SPIM_AddDevice(spi_host_device_t Host, const spi_device_interface_config_t *p_Dev_Config,
                         spi_device_handle_t *p_Handle);

/** @brief          Remove a device from the SPI bus.
 *  @param Host     SPI host device
 *  @param Handle   Device handle to remove
 *  @return         ESP_OK when successful
 */
esp_err_t SPIM_RemoveDevice(spi_host_device_t Host, spi_device_handle_t Handle);

/** @brief      Check if SPI host is initialized.
 *  @param Host SPI host device
 *  @return     true if bus is initialized, false otherwise
 */
bool SPIM_IsInitialized(spi_host_device_t Host);

/** @brief              Transmit data over SPI.
 *  @param Host         SPI host device
 *  @param Handle       Device handle
 *  @param p_Tx_Data    Pointer to data to transmit
 *  @param p_Rx_Data    Pointer to buffer for received data (or NULL if not needed)
 *  @param Length       Length of data to transmit/receive in bytes
 *  @return             ESP_OK when successful
 */
esp_err_t SPIM_Transmit(spi_host_device_t Host, spi_device_handle_t Handle, uint8_t *p_Tx_Data, uint8_t *p_Rx_Data,
                        size_t Length);

#endif /* SPI_H_ */
