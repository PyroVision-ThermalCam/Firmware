/*
 * max17048.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: MAX17048 Li-Ion fuel gauge driver implementation.
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

#include "max17048.h"

#include <sdkconfig.h>

/** @brief MAX17048 I2C address (fixed, not configurable).
 */
#define MAX17048_I2C_ADDR                   0x36

/** @brief MAX17048 register addresses.
 */
#define MAX17048_REG_VCELL                  0x02
#define MAX17048_REG_SOC                    0x04
#define MAX17048_REG_MODE                   0x06
#define MAX17048_REG_VERSION                0x08
#define MAX17048_REG_HIBRT                  0x0A
#define MAX17048_REG_CONFIG                 0x0C
#define MAX17048_REG_VALERT                 0x14
#define MAX17048_REG_CRATE                  0x16
#define MAX17048_REG_VRESET                 0x18
#define MAX17048_REG_STATUS                 0x1A
#define MAX17048_REG_CMD                    0xFE

/** @brief MODE register bit masks.
 */
#define MAX17048_MODE_QUICK_START           0x4000

/** @brief STATUS register bit masks.
 */
#define MAX17048_STATUS_ALERT_MASK          0x3F

/** @brief IC version: top 12 bits are always 0x001.
 */
#define MAX17048_VERSION_EXPECTED           0x0010
#define MAX17048_VERSION_MASK               0xFFF0

/** @brief POR command value written to CMD register to trigger soft reset.
 */
#define MAX17048_CMD_POR                    0x5400

/** @brief VCELL resolution: 1.25 mV per LSB (12-bit result in bits [15:4]).
 */
#define MAX17048_VCELL_LSB_MV               0.00125f

/** @brief SOC resolution: 1/256 percent per LSB.
 */
#define MAX17048_SOC_LSB                    256.0f

/** @brief CRATE resolution: 0.208 %/hr per LSB.
 */
#define MAX17048_CRATE_LSB                  0.208f

static const i2c_device_config_t _MAX17048_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = MAX17048_I2C_ADDR,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const char *TAG = "MAX17048";

/** @brief              Write a 16-bit register to the MAX17048.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param Value        16-bit value to write (MSB first)
 *  @return             ESP_OK on success
 */
static esp_err_t MAX17048_Write_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint16_t Value)
{
    uint8_t Buffer[3];

    Buffer[0] = RegAddr;
    Buffer[1] = static_cast<uint8_t>((Value >> 8) & 0xFF);
    Buffer[2] = static_cast<uint8_t>(Value & 0xFF);

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Read a 16-bit register from the MAX17048.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param p_Value      Pointer to store the read value (MSB first)
 *  @return             ESP_OK on success
 */
static esp_err_t MAX17048_Read_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint16_t *p_Value)
{
    uint8_t Buffer[2];
    esp_err_t Error;

    Error = I2CM_Write(p_Dev_Handle, &RegAddr, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(p_Dev_Handle, Buffer, sizeof(Buffer));
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Value = (static_cast<uint16_t>(Buffer[0]) << 8) | static_cast<uint16_t>(Buffer[1]);

    return ESP_OK;
}

esp_err_t MAX17048_Init(i2c_master_bus_handle_t *p_Bus_Handle, MAX17048_Dev_t *p_Device)
{
    esp_err_t Error;
    uint16_t Version;

    if ((p_Bus_Handle == NULL) || (p_Device == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_MAX17048_I2C_Config, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d!", Error);

        return Error;
    }

    /* The device does not ACK this command - I2C error after write is expected */
    Error = MAX17048_Write_Register(&p_Device->Handle, MAX17048_REG_CMD, MAX17048_CMD_POR);
    if (Error == ESP_OK) {
        /* An ACK would be unexpected; log a warning but treat as success */
        ESP_LOGW(TAG, "Unexpected ACK after POR command");
    }

    /* Wait for reset to complete before allowing further I2C access */
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGD(TAG, "MAX17048 soft reset completed");

    /* Verify IC version: top 12 bits must always equal 0x001 */
    Error = MAX17048_GetICVersion(p_Device, &Version);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read IC version: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    if ((Version & MAX17048_VERSION_MASK) != MAX17048_VERSION_EXPECTED) {
        ESP_LOGE(TAG, "Unexpected IC version: 0x%04X!", Version);

        i2c_master_bus_rm_device(p_Device->Handle);

        return ESP_ERR_NOT_FOUND;
    }

    /* Clear the power-on reset alert flag set automatically at each power-up */
    Error = MAX17048_ClearAlertFlags(p_Device, MAX17048_ALERT_RESET);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear reset alert flag: %d!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "MAX17048 initialized (version: 0x%04X)", Version);

    return ESP_OK;
}

esp_err_t MAX17048_Deinit(MAX17048_Dev_t *p_Device)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Device->Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_bus_rm_device(p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "MAX17048 deinitialized");

    return ESP_OK;
}

esp_err_t MAX17048_GetVoltage(MAX17048_Dev_t *p_Device, float *p_Voltage)
{
    uint16_t Raw;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Voltage == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_VCELL, &Raw);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read VCELL register: %d!", Error);

        return Error;
    }

    /* Bits [15:4] hold the 12-bit ADC result; resolution is 1.25 mV per LSB */
    *p_Voltage = static_cast<float>(Raw >> 4) * MAX17048_VCELL_LSB_MV;

    ESP_LOGD(TAG, "Battery voltage: %.3f V (Raw: 0x%04X)", *p_Voltage, Raw);

    return ESP_OK;
}

esp_err_t MAX17048_GetSOC(MAX17048_Dev_t *p_Device, float *p_SOC)
{
    uint16_t Raw;
    float SOC;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_SOC == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_SOC, &Raw);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SOC register: %d!", Error);

        return Error;
    }

    /* High byte = integer percent, low byte = 1/256 fractional percent */
    SOC = static_cast<float>(Raw) / MAX17048_SOC_LSB;

    /* Clamp to 0–100% to handle ModelGauge edge cases */
    if (SOC < 0.0f) {
        SOC = 0.0f;
    } else if (SOC > 100.0f) {
        SOC = 100.0f;
    }

    *p_SOC = SOC;

    ESP_LOGD(TAG, "Battery SOC: %.2f %% (Raw: 0x%04X)", *p_SOC, Raw);

    return ESP_OK;
}

esp_err_t MAX17048_GetChargeRate(MAX17048_Dev_t *p_Device, float *p_Rate)
{
    uint16_t Raw;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Rate == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_CRATE, &Raw);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read CRATE register: %d!", Error);

        return Error;
    }

    /* CRATE is a signed 16-bit value; positive = charging, negative = discharging */
    *p_Rate = static_cast<float>(static_cast<int16_t>(Raw)) * MAX17048_CRATE_LSB;

    ESP_LOGD(TAG, "Charge rate: %.3f %%/hr (Raw: 0x%04X)", *p_Rate, Raw);

    return ESP_OK;
}

esp_err_t MAX17048_GetICVersion(MAX17048_Dev_t *p_Device, uint16_t *p_Version)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Version == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_VERSION, p_Version);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read VERSION register: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t MAX17048_GetAlertFlags(MAX17048_Dev_t *p_Device, uint8_t *p_Flags)
{
    uint16_t StatusReg;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Flags == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_STATUS, &StatusReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STATUS register: %d!", Error);

        return Error;
    }

    /* Alert flags occupy bits [5:0] of the high byte */
    *p_Flags = static_cast<uint8_t>((StatusReg >> 8) & MAX17048_STATUS_ALERT_MASK);

    return ESP_OK;
}

esp_err_t MAX17048_ClearAlertFlags(MAX17048_Dev_t *p_Device, uint8_t Flags)
{
    uint16_t StatusReg;
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_STATUS, &StatusReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STATUS register: %d!", Error);

        return Error;
    }

    /* Clear only the requested flags in the high byte */
    StatusReg &= ~(static_cast<uint16_t>(Flags & MAX17048_STATUS_ALERT_MASK) << 8);

    Error = MAX17048_Write_Register(&p_Device->Handle, MAX17048_REG_STATUS, StatusReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write STATUS register: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t MAX17048_QuickStart(MAX17048_Dev_t *p_Device)
{
    uint16_t ModeReg;
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_Read_Register(&p_Device->Handle, MAX17048_REG_MODE, &ModeReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE register: %d!", Error);

        return Error;
    }

    ModeReg |= MAX17048_MODE_QUICK_START;

    Error = MAX17048_Write_Register(&p_Device->Handle, MAX17048_REG_MODE, ModeReg);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger quick-start: %d!", Error);

        return Error;
    }

    ESP_LOGI(TAG, "Quick-start triggered");

    return ESP_OK;
}
