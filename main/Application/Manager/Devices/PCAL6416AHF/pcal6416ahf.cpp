/*
 * portexpander.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PCAL6416AHF Port Expander driver implementation.
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
 */

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "pcal6416ahf.h"

#include <sdkconfig.h>

#define ADDR_PCAL6416AHF                    0x20

#define PORT_EXPANDER_REG_INPUT0            0x00
#define PORT_EXPANDER_REG_INPUT1            0x01
#define PORT_EXPANDER_REG_OUTPUT0           0x02
#define PORT_EXPANDER_REG_OUTPUT1           0x03
#define PORT_EXPANDER_REG_POL0              0x04
#define PORT_EXPANDER_REG_POL1              0x05
#define PORT_EXPANDER_REG_CONF0             0x06
#define PORT_EXPANDER_REG_CONF1             0x07
#define PORT_EXPANDER_REG_STRENGTH0_0       0x40
#define PORT_EXPANDER_REG_STRENGTH0_1       0x41
#define PORT_EXPANDER_REG_STRENGTH1_0       0x42
#define PORT_EXPANDER_REG_STRENGTH1_1       0x43
#define PORT_EXPANDER_REG_LATCH0            0x44
#define PORT_EXPANDER_REG_LATCH1            0x45
#define PORT_EXPANDER_REG_PULL_EN0          0x46
#define PORT_EXPANDER_REG_PULL_EN1          0x47
#define PORT_EXPANDER_REG_PULL_SEL0         0x48
#define PORT_EXPANDER_REG_PULL_SEL1         0x49
#define PORT_EXPANDER_REG_INT_MASK0         0x4A
#define PORT_EXPANDER_REG_INT_MASK1         0x4B
#define PORT_EXPANDER_REG_INT_STATUS0       0x4C
#define PORT_EXPANDER_REG_INT_STATUS1       0x4D
#define PORT_EXPANDER_REG_OUT_CONFIG        0x4F

#define PORT_EXPANDER_INPUT                 0x01
#define PORT_EXPANDER_OUTPUT                0x00

/** @brief Battery voltage enable pin connected to port 0, pin 0.
 */
#define PIN_BATTERY_VOLTAGE_ENABLE          0

/** @brief The LDO are connected with port 0, pin 1.
 */
#define PIN_CAMERA                          1

/** @brief The LED is connected with port 0, pin 2.
 */
#define PIN_LED                             2

/** @brief Port expander port number declaration.
 */
typedef enum {
    PORT_0          = 0x00,
    PORT_1          = 0x01,
} PortDefinition_t;

/** @brief Port expander pull option declaration.
 */
typedef enum {
    PULL_PULLDOWN   = 0x00,
    PULL_PULLUP     = 0x01,
} PullDefinition_t;

/** @brief Port expander polarity configuration declaration.
 */
typedef enum {
    POL_NORMAL      = 0x00,
    POL_INVERT      = 0x01,
} PolDefinition_t;

/** @brief Default input polarity inversion configuration for the Port Expander.
 */
static uint8_t DefaultPolarityConfig[] = {
    // Byte 0
    PORT_EXPANDER_REG_POL0,

    // Byte 1 (Polarity 0)
    0x00,

    // Byte 2  (Polarity 1)
    0x00,
};

/** @brief Default input latch configuration for the Port Expander.
 */
static uint8_t DefaultLatchConfig[] = {
    // Byte 0
    PORT_EXPANDER_REG_LATCH0,

    // Byte 1 (Latch 0)
    0x00,

    // Byte 2  (Latch 1)
    0x00,
};

/** @brief Default pull-up / pull-down configuration for the Port Expander.
 */
static uint8_t DefaultPullConfig[] = {
    // Byte 0
    PORT_EXPANDER_REG_PULL_EN0,

    // Byte 1 (Enable 0)
    0x00,

    // Byte 2 (Enable 1)
    0x00,

    // Byte 3 (Select 0)
    0x00,

    // Byte 4 (Select 1)
    0x00,
};

/** @brief Default output pin configuration for the Port Expander.
 */
static uint8_t DefaultPortConfiguration[] = {
    // Byte 0
    PORT_EXPANDER_REG_CONF0,

    // Byte 1 (Config 0)
    (PORT_EXPANDER_OUTPUT << PIN_BATTERY_VOLTAGE_ENABLE) | (PORT_EXPANDER_OUTPUT << PIN_CAMERA) |
    (PORT_EXPANDER_OUTPUT << PIN_LED),

    // Byte 2 (Config 1)
    0x00,
};

/** @brief Default output pin level for the Port Expander.
 */
static uint8_t DefaultPinConfiguration[] = {
    // Byte 0
    PORT_EXPANDER_REG_OUTPUT0,

    // Byte 1 (Output 0)
    // Enable camera power by default (active high for PIN_CAMERA)
    // Battery voltage measurement and LED are off
    (0x01 << PIN_CAMERA),

    // Byte 2 (Output 1)
    0x00,
};

static i2c_device_config_t _Expander_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR_PCAL6416AHF,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static i2c_master_dev_handle_t _Expander_Dev_Handle;

static const char *TAG                      = "PortExpander";

/** @brief          Set the pin level of the pins of a given port.
 *  @param Port     Target port
 *  @param Mask     Pin mask
 *  @param Level    Pin level
 *  @return         ESP_OK when successful
 *                  ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                  ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
static esp_err_t PortExpander_SetPinLevel(PortDefinition_t Port, uint8_t Mask, uint8_t Level)
{
    return I2CM_ModifyRegister(&_Expander_Dev_Handle, PORT_EXPANDER_REG_OUTPUT0 + static_cast<uint8_t>(Port), Mask, Level);
}

/** @brief              Enable the interrupts for given pins.
 *  @param Port         Target port
 *  @param Mask         Pin mask
 *  @param EnableMask   Interrupt enable mask
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
static esp_err_t PortExpander_SetInterruptMask(PortDefinition_t Port, uint8_t Mask, uint8_t EnableMask)
{
    return I2CM_ModifyRegister(&_Expander_Dev_Handle, PORT_EXPANDER_REG_INT_MASK0 + static_cast<uint8_t>(Port), Mask,
                               ~EnableMask);
}

#ifdef DEBUG
void PortExpander_DumpRegister(void)
{
    uint8_t Data;

    ESP_LOGI(TAG, "Register dump:");

    for (uint8_t i = 0x00; i < 0x08; i++) {
        I2CM_Write(&_Expander_Dev_Handle, &i, sizeof(i));
        I2CM_Read(&_Expander_Dev_Handle, &Data, sizeof(Data));
        ESP_LOGI(TAG, "    Register 0x%X: 0x%X", i, Data);
    }

    for (uint8_t i = 0x40; i < 0x4F; i++) {
        I2CM_Write(&_Expander_Dev_Handle, &i, sizeof(i));
        I2CM_Read(&_Expander_Dev_Handle, &Data, sizeof(Data));
        ESP_LOGI(TAG, "    Register 0x%X: 0x%X", i, Data);
    }
}
#endif

esp_err_t PortExpander_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *Bus_Handle)
{
    esp_err_t Error;

    Error = i2c_master_bus_add_device(*Bus_Handle, &_Expander_I2C_Config, &_Expander_Dev_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d!", Error);
        return Error;
    }

    ESP_LOGI(TAG, "Configure Port Expander...");

    return I2CM_Write(&_Expander_Dev_Handle, DefaultPortConfiguration, sizeof(DefaultPortConfiguration)) ||
           I2CM_Write(&_Expander_Dev_Handle, DefaultPinConfiguration, sizeof(DefaultPinConfiguration)) ||
           I2CM_Write(&_Expander_Dev_Handle, DefaultPullConfig, sizeof(DefaultPullConfig)) ||
           I2CM_Write(&_Expander_Dev_Handle, DefaultLatchConfig, sizeof(DefaultLatchConfig)) ||
           I2CM_Write(&_Expander_Dev_Handle, DefaultPolarityConfig, sizeof(DefaultPolarityConfig));
}

esp_err_t PortExpander_Deinit(void)
{
    if (_Expander_Dev_Handle != NULL) {
        esp_err_t Error = i2c_master_bus_rm_device(_Expander_Dev_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);
            return Error;
        }

        _Expander_Dev_Handle = NULL;
    }

    return ESP_OK;
}

esp_err_t PortExpander_EnableCamera(bool Enable)
{
    return PortExpander_SetPinLevel(PORT_0, (0x01 << PIN_CAMERA), (!Enable << PIN_CAMERA));
}

esp_err_t PortExpander_EnableLED(bool Enable)
{
    return PortExpander_SetPinLevel(PORT_0, (0x01 << PIN_LED), (Enable << PIN_LED));
}

esp_err_t PortExpander_EnableBatteryVoltage(bool Enable)
{
    return PortExpander_SetPinLevel(PORT_0, (0x01 << PIN_BATTERY_VOLTAGE_ENABLE), (!Enable << PIN_BATTERY_VOLTAGE_ENABLE));
}
