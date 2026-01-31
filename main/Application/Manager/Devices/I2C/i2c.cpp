/*
 * i2c.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: I2C bus manager implementation.
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
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include "i2c.h"

#include <sdkconfig.h>

#define I2C_READ_ADDR(Addr)                 ((Addr << 0x01) | I2C_MASTER_READ)
#define I2C_WRITE_ADDR(Addr)                ((Addr << 0x01) | I2C_MASTER_WRITE)
#define I2C_WAIT                            100 / portTICK_PERIOD_MS

static SemaphoreHandle_t _I2C_Mutex;

static const char *TAG                      = "I2C";

/** @brief                  Initialize I2C master bus.
 *  @param p_Config         Pointer to I2C bus configuration
 *  @param p_Bus_Handle     Pointer to store bus handle
 *  @return                 ESP_OK on success, error code otherwise
 */
int32_t I2CM_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *p_Bus_Handle)
{
    _I2C_Mutex = xSemaphoreCreateMutex();

    if (_I2C_Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex!");
        return ESP_ERR_NO_MEM;
    }

    return i2c_new_master_bus(p_Config, p_Bus_Handle);
}

/** @brief              Deinitialize I2C master bus.
 *  @param Bus_Handle   I2C bus handle
 *  @return             ESP_OK on success, error code otherwise
 */
int32_t I2CM_Deinit(i2c_master_bus_handle_t Bus_Handle)
{
    vSemaphoreDelete(_I2C_Mutex);

    return i2c_del_master_bus(Bus_Handle);
}

/** @brief              Write data to I2C device.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param p_Data       Pointer to data to write
 *  @param Length       Number of bytes to write
 *  @return             ESP_OK on success, error code otherwise
 */
int32_t I2CM_Write(i2c_master_dev_handle_t *p_Dev_Handle, const uint8_t *p_Data, uint32_t Length)
{
    esp_err_t Error;

    if ((p_Dev_Handle == NULL) || (*p_Dev_Handle == NULL) || (p_Data == NULL)) {
        ESP_LOGE(TAG, "I2C Write: Invalid handle or data pointer");
        return ESP_ERR_INVALID_ARG;
    } else if (Length == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Write %u bytes:", static_cast<unsigned int>(Length));
    for (uint8_t i = 0; i < Length; i++) {
        ESP_LOGD(TAG, "     Byte %u: 0x%02X", static_cast<unsigned int>(i), *(p_Data + i));
    }

    Error = ESP_FAIL;

    if (xSemaphoreTake(_I2C_Mutex, portMAX_DELAY) == pdTRUE) {
        Error = i2c_master_transmit(*p_Dev_Handle, p_Data, Length, I2C_WAIT);
        xSemaphoreGive(_I2C_Mutex);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "I2C transmit failed: %d", Error);
        }
    }

    return Error;
}

/** @brief              Read data from I2C device.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param p_Data       Pointer to buffer for received data
 *  @param Length       Number of bytes to read
 *  @return             ESP_OK on success, error code otherwise
 */
int32_t I2CM_Read(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t *p_Data, uint32_t Length)
{
    esp_err_t Error;

    if ((p_Dev_Handle == NULL) || (*p_Dev_Handle == NULL) || (p_Data == NULL)) {
        ESP_LOGE(TAG, "I2C Read: Invalid handle or data pointer");
        return ESP_ERR_INVALID_ARG;
    } else if (Length == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Read %u bytes:", static_cast<unsigned int>(Length));

    for (uint8_t i = 0; i < Length; i++) {
        ESP_LOGD(TAG, "     Byte %u: 0x%02X", static_cast<unsigned int>(i), *(p_Data + i));
    }

    Error = ESP_FAIL;

    if (xSemaphoreTake(_I2C_Mutex, portMAX_DELAY) == pdTRUE) {
        Error = i2c_master_receive(*p_Dev_Handle, p_Data, Length, I2C_WAIT);
        xSemaphoreGive(_I2C_Mutex);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "I2C receive failed: %d", Error);
        }
    }

    return Error;
}

int32_t I2CM_ModifyRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t Mask, uint8_t Value)
{
    int32_t Error;
    uint8_t Data[2] = {Register, 0xFF};

    Error = I2CM_Write(p_Dev_Handle, &Data[0], 1) || I2CM_Read(p_Dev_Handle, &Data[1], 1);
    if (Error) {
        return Error;
    }

    Data[1] &= ~Mask;
    Data[1] |= Value;
    ESP_LOGD(TAG, "Modify Register 0x%02X with mask 0x%02X: 0x%02X", Register, Mask, Data[1]);

    return I2CM_Write(p_Dev_Handle, Data, sizeof(Data));
}