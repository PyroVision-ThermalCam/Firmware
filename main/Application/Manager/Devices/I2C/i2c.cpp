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

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "i2c.h"

#include <sdkconfig.h>

#define I2C_READ_ADDR(Addr)                 ((Addr << 0x01) | I2C_MASTER_READ)
#define I2C_WRITE_ADDR(Addr)                ((Addr << 0x01) | I2C_MASTER_WRITE)
#define I2C_WAIT                            pdMS_TO_TICKS(100)

static const char *TAG                      = "I2C";

/** @brief                  Initialize I2C master bus.
 *  @param p_Config         Pointer to I2C bus configuration
 *  @param p_Bus_Handle     Pointer to store bus handle
 *  @return                 ESP_OK on success, error code otherwise
 */
int32_t I2CM_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *p_Bus_Handle)
{
    return i2c_new_master_bus(p_Config, p_Bus_Handle);
}

/** @brief              Deinitialize I2C master bus.
 *  @param Bus_Handle   I2C bus handle
 *  @return             ESP_OK on success, error code otherwise
 */
int32_t I2CM_Deinit(i2c_master_bus_handle_t Bus_Handle)
{
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
        return ESP_ERR_INVALID_ARG;
    } else if (Length == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Write %u bytes:", static_cast<unsigned int>(Length));
    for (uint32_t i = 0; i < Length; i++) {
        ESP_LOGD(TAG, "     Byte %u: 0x%02X", static_cast<unsigned int>(i), *(p_Data + i));
    }

    Error = i2c_master_transmit(*p_Dev_Handle, p_Data, Length, I2C_WAIT);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "I2C transmit failed: 0x%X", Error);
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
        return ESP_ERR_INVALID_ARG;
    } else if (Length == 0) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Read %u bytes:", static_cast<unsigned int>(Length));

    for (uint32_t i = 0; i < Length; i++) {
        ESP_LOGD(TAG, "     Byte %u: 0x%02X", static_cast<unsigned int>(i), *(p_Data + i));
    }

    Error = i2c_master_receive(*p_Dev_Handle, p_Data, Length, I2C_WAIT);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "I2C receive failed: 0x%X!", Error);
    }

    return Error;
}

int32_t I2CM_WriteRead(i2c_master_dev_handle_t *p_Dev_Handle,
                       const uint8_t *p_WriteData, uint32_t WriteLength,
                       uint8_t *p_ReadData, uint32_t ReadLength)
{
    esp_err_t Error;

    if ((p_Dev_Handle == NULL) || (*p_Dev_Handle == NULL) || (p_WriteData == NULL) || (p_ReadData == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if ((WriteLength == 0) || (ReadLength == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_transmit_receive(*p_Dev_Handle, p_WriteData, WriteLength, p_ReadData, ReadLength, I2C_WAIT);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "I2C WriteRead failed: 0x%X", Error);
    }

    return Error;
}

int32_t I2CM_ModifyRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t Mask, uint8_t Value)
{
    esp_err_t Error;
    uint8_t RegAddr = Register;
    uint8_t RegValue = 0xFF;
    uint8_t Data[2] = { Register, 0 };

    if ((p_Dev_Handle == NULL) || (*p_Dev_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_transmit_receive(*p_Dev_Handle, &RegAddr, 1, &RegValue, 1, I2C_WAIT);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "I2C ModifyRegister: read failed: 0x%X", Error);

        return Error;
    }

    RegValue &= ~Mask;
    RegValue |= Value;

    Data[1] = RegValue;

    ESP_LOGD(TAG, "Modify Register 0x%02X with mask 0x%02X: 0x%02X", Register, Mask, RegValue);

    Error = i2c_master_transmit(*p_Dev_Handle, Data, sizeof(Data), I2C_WAIT);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "I2C ModifyRegister: write-back failed: 0x%X", Error);
    }

    return Error;
}

int32_t I2CM_Scan(i2c_master_bus_handle_t Bus_Handle)
{
    /* I2C reserved address ranges: 0x00-0x07 and 0x78-0x7F */
    const uint8_t I2C_SCAN_ADDR_MIN = 0x08;
    const uint8_t I2C_SCAN_ADDR_MAX = 0x77;

    uint8_t DevicesFound = 0;

    if (Bus_Handle == NULL) {
        ESP_LOGE(TAG, "I2C Scan: Invalid bus handle!");

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Scanning I2C bus...");
    ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");

    for (uint8_t Row = 0; Row < 8; Row++) {
        char Line[52];
        int Offset = 0;

        for (uint8_t Col = 0; Col < 16; Col++) {
            uint8_t Addr;

            Addr = static_cast<uint8_t>((Row * 16) + Col);
            if ((Addr < I2C_SCAN_ADDR_MIN) || (Addr > I2C_SCAN_ADDR_MAX)) {
                Offset += snprintf(Line + Offset, sizeof(Line) - static_cast<size_t>(Offset), "-- ");
            } else {
                esp_err_t Error;

                Error = i2c_master_probe(Bus_Handle, Addr, 50);
                if (Error == ESP_OK) {
                    Offset += snprintf(Line + Offset, sizeof(Line) - static_cast<size_t>(Offset), "%02X ", Addr);
                    DevicesFound++;
                } else {
                    Offset += snprintf(Line + Offset, sizeof(Line) - static_cast<size_t>(Offset), "-- ");
                }
            }
        }

        ESP_LOGI(TAG, "%02X: %s", Row * 16, Line);
    }

    ESP_LOGI(TAG, "I2C scan complete. %u device(s) found.", static_cast<unsigned int>(DevicesFound));

    /* Reset bus to recover from any timeout states that occurred during probing.
     * A short delay after the reset allows the I2C driver to complete its internal
     * state machine recovery before the next transfer is queued. */
    i2c_master_bus_reset(Bus_Handle);
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}