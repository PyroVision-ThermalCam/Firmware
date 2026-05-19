/*
 * tmp117.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: TMP117 Temperature Sensor driver implementation.
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

#include "tmp117.h"

#include <sdkconfig.h>

/** @brief TMP117 I2C address (ADD0 pin connected to GND).
 */
#define TMP117_I2C_ADDR                     0x48

/** @brief TMP117 register addresses.
 */
#define TMP117_REG_TEMP_RESULT              0x00
#define TMP117_REG_CONFIGURATION            0x01
#define TMP117_REG_T_HIGH_LIMIT             0x02
#define TMP117_REG_T_LOW_LIMIT              0x03
#define TMP117_REG_EEPROM_UL                0x04
#define TMP117_REG_EEPROM1                  0x05
#define TMP117_REG_EEPROM2                  0x06
#define TMP117_REG_TEMP_OFFSET              0x07
#define TMP117_REG_EEPROM3                  0x08
#define TMP117_REG_DEVICE_ID                0x0F

/** @brief Configuration register bit masks.
 */
#define TMP117_CFG_MOD_MASK                 0x0C00
#define TMP117_CFG_MOD_SHIFT                10
#define TMP117_CFG_CONV_MASK                0x01C0
#define TMP117_CFG_CONV_SHIFT               7
#define TMP117_CFG_AVG_MASK                 0x0060
#define TMP117_CFG_AVG_SHIFT                5
#define TMP117_CFG_DATA_READY               0x2000
#define TMP117_CFG_SOFT_RESET               0x0002

/** @brief TMP117 device ID expected value.
 */
#define TMP117_DEVICE_ID                    0x0117

/** @brief Temperature resolution in °C per LSB.
 */
#define TMP117_RESOLUTION                   0.0078125f

/** @brief TMP117 I2C device configuration (7-bit address, 400 kHz, ACK check enabled).
 */
static const i2c_device_config_t _TMP117_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = TMP117_I2C_ADDR,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

/** @brief Configure default settings:
 *      - Continuous mode
 *      - 1 second cycle
 *      - No averaging
 */
static const TMP117_Config_t _TMP117_DefaultConfig = {
    .Mode = TMP117_MODE_CONTINUOUS,
    .Cycle = TMP117_CYCLE_1S,
    .Averaging = TMP117_AVG_NONE,
};

static const char *TAG = "TMP117";

/** @brief              Write a 16-bit register to TMP117.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param Value        16-bit value to write
 *  @return             ESP_OK on success
 */
static esp_err_t TMP117_Write_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint16_t Value)
{
    uint8_t Buffer[3];

    Buffer[0] = RegAddr;
    Buffer[1] = static_cast<uint8_t>((Value >> 8) & 0xFF);
    Buffer[2] = static_cast<uint8_t>(Value & 0xFF);

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Read a 16-bit register from TMP117.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param p_Value      Pointer to store the read value
 *  @return             ESP_OK on success
 */
static esp_err_t TMP117_Read_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint16_t *p_Value)
{
    uint8_t Buffer[2];
    esp_err_t Error;

    Error = I2CM_WriteRead(p_Dev_Handle, &RegAddr, 1, Buffer, sizeof(Buffer));
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Value = (static_cast<uint16_t>(Buffer[0]) << 8) | static_cast<uint16_t>(Buffer[1]);

    return ESP_OK;
}

esp_err_t TMP117_Init(i2c_master_bus_handle_t *p_Bus_Handle, TMP117_Dev_t *p_Device,
                      const TMP117_Config_t *p_Config)
{
    esp_err_t Error;
    uint16_t Temp;
    TMP117_Config_t *Config;

    if ((p_Bus_Handle == NULL) || (p_Device == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_TMP117_I2C_Config, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: 0x%X!", Error);

        return Error;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, &Temp);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read configuration register: 0x%X!", Error);

        return Error;
    }

    Temp |= TMP117_CFG_SOFT_RESET;
    Error = TMP117_Write_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, Temp);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write soft reset: 0x%X!", Error);

        return Error;
    }

    /* Wait for reset to complete (minimum 2 ms) */
    vTaskDelay(pdMS_TO_TICKS(5));

    Error = TMP117_ReadDeviceID(p_Device, &Temp);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device ID: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    if (Temp != TMP117_DEVICE_ID) {
        ESP_LOGE(TAG, "Invalid device ID: 0x%04X (expected 0x%04X)!", Temp, TMP117_DEVICE_ID);

        i2c_master_bus_rm_device(p_Device->Handle);

        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "TMP117 initialized successfully (ID: 0x%04X)", Temp);

    Config = const_cast<TMP117_Config_t *>(&_TMP117_DefaultConfig);
    if (p_Config != NULL) {
        Config = const_cast<TMP117_Config_t *>(p_Config);

        ESP_LOGW(TAG, "Using custom configuration: Mode = %d, Cycle = %d, Avg = %d",
                 static_cast<int>(Config->Mode), static_cast<int>(Config->Cycle), static_cast<int>(Config->Averaging));
    }

    Error = TMP117_Configure(p_Device, Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure default settings: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    p_Device->IsInitialized = true;

    return ESP_OK;
}

esp_err_t TMP117_Deinit(TMP117_Dev_t *p_Device)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Device->Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_bus_rm_device(p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "TMP117 deinitialized");

    p_Device->IsInitialized = false;
    p_Device->Handle = NULL;

    return ESP_OK;
}

esp_err_t TMP117_Configure(TMP117_Dev_t *p_Device, const TMP117_Config_t *p_Config)
{
    uint16_t ConfigReg;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Config == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, &ConfigReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read configuration register: 0x%X!", Error);

        return Error;
    }

    /* Clear mode, conversion cycle, and averaging bits */
    ConfigReg &= ~(TMP117_CFG_MOD_MASK | TMP117_CFG_CONV_MASK | TMP117_CFG_AVG_MASK);

    ConfigReg |= ((static_cast<uint16_t>(p_Config->Mode) << TMP117_CFG_MOD_SHIFT) & TMP117_CFG_MOD_MASK);
    ConfigReg |= ((static_cast<uint16_t>(p_Config->Cycle) << TMP117_CFG_CONV_SHIFT) & TMP117_CFG_CONV_MASK);
    ConfigReg |= ((static_cast<uint16_t>(p_Config->Averaging) << TMP117_CFG_AVG_SHIFT) & TMP117_CFG_AVG_MASK);

    Error = TMP117_Write_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, ConfigReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write configuration register: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "TMP117 configured: Mode=%d, Cycle=%d, Avg=%d",
             static_cast<int>(p_Config->Mode), static_cast<int>(p_Config->Cycle), static_cast<int>(p_Config->Averaging));

    return ESP_OK;
}

esp_err_t TMP117_ReadTemperature(TMP117_Dev_t *p_Device, float *p_Temp)
{
    uint16_t TempRaw;
    int16_t TempSigned;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Temp == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_TEMP_RESULT, &TempRaw);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature register: 0x%X!", Error);

        return Error;
    }

    /* Convert to signed 16-bit value */
    TempSigned = static_cast<int16_t>(TempRaw);

    /* Apply resolution (0.0078125°C per LSB) */
    *p_Temp = static_cast<float>(TempSigned) * TMP117_RESOLUTION;

    ESP_LOGD(TAG, "Temperature: %.4f°C (Raw: 0x%04X)", *p_Temp, TempRaw);

    return ESP_OK;
}

esp_err_t TMP117_TriggerOneShot(TMP117_Dev_t *p_Device)
{
    uint16_t ConfigReg;
    esp_err_t Error;
    uint8_t CurrentMode;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, &ConfigReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read configuration register: 0x%X!", Error);

        return Error;
    }

    /* Check if in one-shot mode */
    CurrentMode = static_cast<uint8_t>((ConfigReg & TMP117_CFG_MOD_MASK) >> TMP117_CFG_MOD_SHIFT);
    if (CurrentMode != TMP117_MODE_ONE_SHOT) {
        ESP_LOGE(TAG, "Device not in one-shot mode (current mode: %d)!", CurrentMode);

        return ESP_ERR_INVALID_STATE;
    }

    /* Trigger one-shot conversion by setting mode bits back to one-shot */
    ConfigReg &= ~TMP117_CFG_MOD_MASK;
    ConfigReg |= (static_cast<uint16_t>(TMP117_MODE_ONE_SHOT) << TMP117_CFG_MOD_SHIFT) & TMP117_CFG_MOD_MASK;

    Error = TMP117_Write_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, ConfigReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger one-shot conversion: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "One-shot conversion triggered");

    return ESP_OK;
}

esp_err_t TMP117_IsDataReady(TMP117_Dev_t *p_Device, bool *p_Ready)
{
    uint16_t Temp;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Ready == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_CONFIGURATION, &Temp);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read configuration register: 0x%X!", Error);

        return Error;
    }

    *p_Ready = (Temp & TMP117_CFG_DATA_READY) != 0;

    return ESP_OK;
}

esp_err_t TMP117_ReadDeviceID(TMP117_Dev_t *p_Device, uint16_t *p_DeviceID)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_DeviceID == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = TMP117_Read_Register(&p_Device->Handle, TMP117_REG_DEVICE_ID, p_DeviceID);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device ID register: 0x%X!", Error);

        return Error;
    }

    return ESP_OK;
}
