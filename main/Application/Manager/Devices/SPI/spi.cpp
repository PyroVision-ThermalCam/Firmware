/*
 * spi.cpp
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

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "spi.h"

#include <sdkconfig.h>

typedef struct {
    bool isInitialized;
    SemaphoreHandle_t Mutex;
    uint32_t DeviceCount;
} SPI_Bus_State_t;

static SPI_Bus_State_t _SPI_State[SOC_SPI_PERIPH_NUM];

static const char *TAG = "spi-manager";

/** @brief              Initialize SPI bus.
 *  @param p_Config     Pointer to SPI bus configuration
 *  @param Host         SPI host device
 *  @param DMA_Channel  DMA channel to use
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t SPIM_Init(const spi_bus_config_t *p_Config, spi_host_device_t Host, int DMA_Channel)
{
    esp_err_t Error;

    if ((p_Config == NULL) || (Host >= SOC_SPI_PERIPH_NUM)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if already initialized */
    if (_SPI_State[Host].isInitialized) {
        ESP_LOGW(TAG, "SPI%d already initialized", Host + 1);

        return ESP_ERR_INVALID_STATE;
    }

    /* Create mutex for bus access */
    _SPI_State[Host].Mutex = xSemaphoreCreateMutex();
    if (_SPI_State[Host].Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI%d mutex!", Host + 1);

        return ESP_ERR_NO_MEM;
    }

    /* Initialize the SPI bus */
    Error = spi_bus_initialize(Host, p_Config, DMA_Channel);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI%d bus: %d", Host + 1, Error);

        vSemaphoreDelete(_SPI_State[Host].Mutex);
        _SPI_State[Host].Mutex = NULL;

        return Error;
    }

    _SPI_State[Host].isInitialized = true;
    _SPI_State[Host].DeviceCount = 0;

    ESP_LOGD(TAG, "SPI%d bus initialized successfully", Host + 1);

    return ESP_OK;
}

/** @brief      Deinitialize SPI bus.
 *  @param Host SPI host device
 *  @return     ESP_OK on success, error code otherwise
 */
esp_err_t SPIM_Deinit(spi_host_device_t Host)
{
    esp_err_t Error;

    if (Host >= SOC_SPI_PERIPH_NUM) {
        return ESP_ERR_INVALID_ARG;
    } else if (_SPI_State[Host].isInitialized == false) {
        return ESP_OK;
    }

    if (_SPI_State[Host].DeviceCount > 0) {
        ESP_LOGW(TAG, "SPI%d still has %d devices attached", Host + 1, _SPI_State[Host].DeviceCount);
    }

    xSemaphoreTake(_SPI_State[Host].Mutex, portMAX_DELAY);
    Error = spi_bus_free(Host);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to free SPI%d bus: %d!", Host + 1, Error);
        xSemaphoreGive(_SPI_State[Host].Mutex);
        return Error;
    }
    xSemaphoreGive(_SPI_State[Host].Mutex);

    if (_SPI_State[Host].Mutex != NULL) {
        vSemaphoreDelete(_SPI_State[Host].Mutex);
        _SPI_State[Host].Mutex = NULL;
    }

    _SPI_State[Host].isInitialized = false;
    _SPI_State[Host].DeviceCount = 0;

    ESP_LOGD(TAG, "SPI%d bus deinitialized", Host + 1);

    return ESP_OK;
}

esp_err_t SPIM_AddDevice(spi_host_device_t Host, const spi_device_interface_config_t *p_Dev_Config,
                         spi_device_handle_t *p_Handle)
{
    esp_err_t Error;

    if ((p_Dev_Config == NULL) || (p_Handle == NULL) || (Host >= SOC_SPI_PERIPH_NUM)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (Host >= SOC_SPI_PERIPH_NUM) {
        ESP_LOGE(TAG, "Invalid SPI host: %d", Host);
        return ESP_ERR_INVALID_ARG;
    }

    if (_SPI_State[Host].isInitialized == false) {
        ESP_LOGE(TAG, "SPI%d bus not initialized!", Host + 1);
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(_SPI_State[Host].Mutex, portMAX_DELAY);
    Error = spi_bus_add_device(Host, p_Dev_Config, p_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to SPI%d: %d!", Host + 1, Error);
        xSemaphoreGive(_SPI_State[Host].Mutex);
        return Error;
    }

    _SPI_State[Host].DeviceCount++;
    xSemaphoreGive(_SPI_State[Host].Mutex);

    ESP_LOGD(TAG, "Device added to SPI%d (total: %d devices)", Host + 1, _SPI_State[Host].DeviceCount);

    return ESP_OK;
}

esp_err_t SPIM_RemoveDevice(spi_host_device_t Host, spi_device_handle_t Handle)
{
    esp_err_t Error;

    if ((Handle == NULL) || (Host >= SOC_SPI_PERIPH_NUM)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_SPI_State[Host].Mutex, portMAX_DELAY);
    Error = spi_bus_remove_device(Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove SPI device: %d!", Error);
        xSemaphoreGive(_SPI_State[Host].Mutex);
        return Error;
    }

    if (_SPI_State[Host].DeviceCount > 0) {
        _SPI_State[Host].DeviceCount--;
    }

    xSemaphoreGive(_SPI_State[Host].Mutex);

    ESP_LOGD(TAG, "SPI device removed");

    return ESP_OK;
}

bool SPIM_IsInitialized(spi_host_device_t Host)
{
    if (Host >= SOC_SPI_PERIPH_NUM) {
        return false;
    }

    return _SPI_State[Host].isInitialized;
}

esp_err_t SPIM_Transmit(spi_host_device_t Host, spi_device_handle_t Handle, uint8_t *p_Tx_Data, uint8_t *p_Rx_Data,
                        size_t Length)
{
    esp_err_t Error;
    spi_transaction_t trans;

    if ((Handle == NULL) || (Host >= SOC_SPI_PERIPH_NUM)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&trans, 0, sizeof(trans));
    trans.tx_buffer = p_Tx_Data;
    trans.rx_buffer = p_Rx_Data;
    trans.length = Length * 8;

    xSemaphoreTake(_SPI_State[Host].Mutex, portMAX_DELAY);
    Error = spi_device_polling_transmit(Handle, &trans);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %d!", Error);
        xSemaphoreGive(_SPI_State[Host].Mutex);
        return Error;
    }
    xSemaphoreGive(_SPI_State[Host].Mutex);

    return ESP_OK;
}