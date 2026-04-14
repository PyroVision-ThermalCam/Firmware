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

#include "pcal6416ahf.h"

#define PORT_EXPANDER_REG_INPUT0            0x00
#define PORT_EXPANDER_REG_INPUT1            0x01
#define PORT_EXPANDER_REG_OUTPUT0           0x02
#define PORT_EXPANDER_REG_OUTPUT1           0x03
#define PORT_EXPANDER_REG_POL0              0x04
#define PORT_EXPANDER_REG_POL1              0x05
#define PORT_EXPANDER_REG_CONF0             0x06
#define PORT_EXPANDER_REG_CONF1             0x07
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

static const char *TAG = "PortExpander";

/** @brief              Iterates over the pin configuration table and writes all register groups
 *                      (OUTPUT reset, polarity, direction, pull resistors, input latch,
 *                      interrupt mask) to the device.
 *  @param p_Dev_Handle Pointer to the I2C device handle
 *  @param p_Config     Pointer to the pin configuration array
 *  @param Count        Number of entries in p_Config
 *  @return             ESP_OK on success, propagated I2C error otherwise
 */
static esp_err_t PCAL6416AHF_Apply_PinConfig(i2c_master_dev_handle_t *p_Dev_Handle,
                                             const PCAL6416_IO_Conf_t *p_Config, size_t Count)
{
    esp_err_t Error;
    uint8_t Conf[2] = { 0xFF, 0xFF };       /* All inputs by default */
    uint8_t Pol[2] = { 0x00, 0x00 };        /* No polarity inversion */
    uint8_t PullEn[2] = { 0x00, 0x00 };     /* All pulls disabled */
    uint8_t PullSel[2] = { 0x00, 0x00 };    /* Pull-down when enabled */
    uint8_t IntMask[2] = { 0xFF, 0xFF };    /* All interrupts masked - cleared for each input pin */
    uint8_t Latch[2] = { 0x00, 0x00 };     /* Input latch disabled by default */

    for (size_t i = 0; i < Count; i++) {
        uint8_t Bit = static_cast<uint8_t>(0x01u << p_Config[i].Pin);

        if (p_Config[i].Direction == PCAL6416_DIR_OUTPUT) {
            Conf[static_cast<uint8_t>(p_Config[i].Port)] &= ~Bit;
        }

        if (p_Config[i].Direction == PCAL6416_DIR_INPUT) {
            /* Enable interrupt for this input pin (INT_MASK bit = 0 means enabled) */
            IntMask[static_cast<uint8_t>(p_Config[i].Port)] &= ~Bit;
        }

        /* Input latch: when enabled the input value is captured at the interrupt edge
         * and held until the INPUT register is read, preventing metastable reads on
         * mechanically bouncing signals (e.g. card-detect switches). */
        if ((p_Config[i].IsLatched) && (p_Config[i].Direction == PCAL6416_DIR_INPUT)) {
            Latch[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;
        }

        /* Polarity inversion register only affects input pins on PCAL6416AHF */
        if ((p_Config[i].IsInverted) && (p_Config[i].Direction == PCAL6416_DIR_INPUT)) {
            Pol[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;
        }

        if (p_Config[i].Pull != PCAL6416_PULL_NONE) {
            PullEn[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;

            if (p_Config[i].Pull == PCAL6416_PULL_UP) {
                PullSel[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;
            }
        }
    }

    /* Set all output pins low before enabling outputs */
    uint8_t OutputBuf[] = { PORT_EXPANDER_REG_OUTPUT0, 0x00, 0x00 };
    Error = I2CM_Write(p_Dev_Handle, OutputBuf, sizeof(OutputBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure polarity inversion (input pins only) */
    uint8_t PolBuf[] = { PORT_EXPANDER_REG_POL0, Pol[0], Pol[1] };
    Error = I2CM_Write(p_Dev_Handle, PolBuf, sizeof(PolBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure pin direction */
    uint8_t ConfBuf[] = { PORT_EXPANDER_REG_CONF0, Conf[0], Conf[1] };
    Error = I2CM_Write(p_Dev_Handle, ConfBuf, sizeof(ConfBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure pull resistors (PULL_EN0/1 + PULL_SEL0/1 are sequential) */
    uint8_t PullBuf[] = { PORT_EXPANDER_REG_PULL_EN0, PullEn[0], PullEn[1], PullSel[0], PullSel[1] };
    Error = I2CM_Write(p_Dev_Handle, PullBuf, sizeof(PullBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure input latch (LATCH0/1 are sequential) */
    uint8_t LatchBuf[] = { PORT_EXPANDER_REG_LATCH0, Latch[0], Latch[1] };
    Error = I2CM_Write(p_Dev_Handle, LatchBuf, sizeof(LatchBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure interrupt mask (bit = 0: enabled, bit = 1: masked; all input pins enabled) */
    uint8_t IntMaskBuf[] = { PORT_EXPANDER_REG_INT_MASK0, IntMask[0], IntMask[1] };
    Error = I2CM_Write(p_Dev_Handle, IntMaskBuf, sizeof(IntMaskBuf));
    if (Error != ESP_OK) {
        return Error;
    }

    return ESP_OK;
}

esp_err_t PCAL6416AHF_Init(i2c_master_bus_handle_t *p_Bus_Handle, uint8_t Address,
                           const PCAL6416_IO_Conf_t *p_Config, size_t Count,
                           PCAL6416AHF_Dev_t *p_Device)
{
    esp_err_t Error;
    uint8_t Temp[2];

    if ((p_Bus_Handle == NULL) || (p_Config == NULL) || (p_Device == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_device_config_t Config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = Address,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &Config, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device (addr=0x%02X): 0x%X!", Address, Error);

        return Error;
    }

    ESP_LOGD(TAG, "Configure Port Expander (addr=0x%02X)...", Address);

    Error = PCAL6416AHF_Apply_PinConfig(&p_Device->Handle, p_Config, Count);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply pin configuration: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    Temp[0] = PORT_EXPANDER_REG_INT_STATUS0;
    if ((I2CM_Write(&p_Device->Handle, &Temp[0], 1) != ESP_OK) ||
        (I2CM_Read(&p_Device->Handle, Temp, 2) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to read initial interrupt status: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    Temp[0] = PORT_EXPANDER_REG_INPUT0;
    if ((I2CM_Write(&p_Device->Handle, &Temp[0], 1) != ESP_OK) ||
        (I2CM_Read(&p_Device->Handle, Temp, 2) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to read initial input status: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    p_Device->IsInitialized = true;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_Deinit(PCAL6416AHF_Dev_t *p_Device)
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

    ESP_LOGD(TAG, "PCAL6416AHF deinitialized");

    p_Device->IsInitialized = false;
    p_Device->Handle = NULL;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_ReadPin(PCAL6416AHF_Dev_t *p_Device, PCAL6416_Port_t Port, uint8_t Pin,
                              bool *p_Level)
{
    esp_err_t Error;
    uint8_t Temp;

    if ((p_Device == NULL) || (p_Level == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Temp = static_cast<uint8_t>(PORT_EXPANDER_REG_INPUT0 + static_cast<uint8_t>(Port));

    Error = I2CM_Write(&p_Device->Handle, &Temp, sizeof(Temp));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, &Temp, sizeof(Temp));
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Level = ((Temp >> Pin) & 0x01) != 0;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_WritePin(PCAL6416AHF_Dev_t *p_Device, PCAL6416_Port_t Port, uint8_t Pin,
                               bool Level)
{
    uint8_t Mask = static_cast<uint8_t>(0x01u << Pin);
    uint8_t Value = Level ? Mask : static_cast<uint8_t>(0x00);

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return I2CM_ModifyRegister(&p_Device->Handle,
                               static_cast<uint8_t>(PORT_EXPANDER_REG_OUTPUT0 + static_cast<uint8_t>(Port)),
                               Mask, Value);
}

esp_err_t PCAL6416AHF_ReadInputs(PCAL6416AHF_Dev_t *p_Device, uint8_t *p_Input0, uint8_t *p_Input1)
{
    esp_err_t Error;
    uint8_t Reg = PORT_EXPANDER_REG_INPUT0;
    uint8_t Buf[2];

    if ((p_Device == NULL) || (p_Input0 == NULL) || (p_Input1 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = I2CM_Write(&p_Device->Handle, &Reg, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, Buf, 2);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Input0 = Buf[0];
    *p_Input1 = Buf[1];

    return ESP_OK;
}

esp_err_t PCAL6416AHF_ReadIntStatus(PCAL6416AHF_Dev_t *p_Device,
                                    uint8_t *p_Status0, uint8_t *p_Status1,
                                    uint8_t *p_Input0, uint8_t *p_Input1)
{
    esp_err_t Error;
    uint8_t Reg;
    uint8_t Buf[2];

    if ((p_Device == NULL) || (p_Status0 == NULL) || (p_Status1 == NULL) ||
        (p_Input0 == NULL) || (p_Input1 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Read INT_STATUS0/1: clears all pending bits and releases INT# (open-drain goes high). */
    Reg = PORT_EXPANDER_REG_INT_STATUS0;

    Error = I2CM_Write(&p_Device->Handle, &Reg, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, Buf, 2);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Status0 = Buf[0];
    *p_Status1 = Buf[1];

    Reg = PORT_EXPANDER_REG_INPUT0;

    Error = I2CM_Write(&p_Device->Handle, &Reg, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, Buf, 2);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Input0 = Buf[0];
    *p_Input1 = Buf[1];

    return ESP_OK;
}
