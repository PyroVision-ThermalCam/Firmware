/*
 * pca9633dp1.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PCA9633DP1 LED Driver implementation.
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

#include "pca9633dp1.h"

#include <sdkconfig.h>

static const char *TAG = "PCA9633DP1";

/** @brief PCA9633 I2C address (ALLCALLADR pin configuration).
 */
#define PCA9633_I2C_ADDR                    0x62

/** @brief PCA9633 register addresses.
 */
#define PCA9633_REG_MODE1                   0x00
#define PCA9633_REG_MODE2                   0x01
#define PCA9633_REG_PWM0                    0x02
#define PCA9633_REG_PWM1                    0x03
#define PCA9633_REG_PWM2                    0x04
#define PCA9633_REG_PWM3                    0x05
#define PCA9633_REG_GRPPWM                  0x06
#define PCA9633_REG_GRPFREQ                 0x07
#define PCA9633_REG_LEDOUT                  0x08
#define PCA9633_REG_SUBADR1                 0x09
#define PCA9633_REG_SUBADR2                 0x0A
#define PCA9633_REG_SUBADR3                 0x0B
#define PCA9633_REG_ALLCALLADR              0x0C

/** @brief MODE1 register bit masks.
 */
#define PCA9633_MODE1_AI2                   0x80    /**< Auto-Increment: All registers. */
#define PCA9633_MODE1_AI1                   0x40    /**< Auto-Increment: Individual brightness only. */
#define PCA9633_MODE1_AI0                   0x20    /**< Auto-Increment: Global control only. */
#define PCA9633_MODE1_SLEEP                 0x10    /**< Low power mode (oscillator off). */
#define PCA9633_MODE1_SUB1                  0x08    /**< Respond to subaddress 1. */
#define PCA9633_MODE1_SUB2                  0x04    /**< Respond to subaddress 2. */
#define PCA9633_MODE1_SUB3                  0x02    /**< Respond to subaddress 3. */
#define PCA9633_MODE1_ALLCALL               0x01    /**< Respond to All Call I2C-bus address. */

/** @brief MODE2 register bit masks.
 */
#define PCA9633_MODE2_DMBLNK                0x20    /**< Group control: blinking (1) or dimming (0). */
#define PCA9633_MODE2_INVRT                 0x10    /**< Output logic state inverted. */
#define PCA9633_MODE2_OCH                   0x08    /**< Outputs change on ACK (0) or STOP (1). */
#define PCA9633_MODE2_OUTDRV                0x04    /**< Output driver: totem pole (1) or open-drain (0). */
#define PCA9633_MODE2_OUTNE1                0x02    /**< Output state when OE=1 (bit 1). */
#define PCA9633_MODE2_OUTNE0                0x01    /**< Output state when OE=1 (bit 0). */

/** @brief LEDOUT register - LED driver output state.
 */
#define PCA9633_LEDOUT_OFF                  0x00    /**< LED driver off. */
#define PCA9633_LEDOUT_ON                   0x01    /**< LED driver fully on (not PWM controlled). */
#define PCA9633_LEDOUT_PWM                  0x02    /**< LED driver individual PWM control. */
#define PCA9633_LEDOUT_GRPPWM               0x03    /**< LED driver group PWM control. */

/** @brief Auto-Increment flag for register access.
 */
#define PCA9633_AUTO_INCREMENT              0x80

static i2c_device_config_t _Device_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = PCA9633_I2C_ADDR,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

/** @brief              Write a single register to PCA9633.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param Value        8-bit value to write
 *  @return             ESP_OK on success
 */
static esp_err_t pca9633_write_register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint8_t Value)
{
    uint8_t Buffer[2];

    Buffer[0] = RegAddr;
    Buffer[1] = Value;

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Read a single register from PCA9633.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Register address
 *  @param p_Value      Pointer to store the read value
 *  @return             ESP_OK on success
 */
static esp_err_t pca9633_read_register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint8_t *p_Value)
{
    esp_err_t Error;

    Error = I2CM_Write(p_Dev_Handle, &RegAddr, 1);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register address 0x%02X: %d!", RegAddr, Error);

        return Error;
    }

    Error = I2CM_Read(p_Dev_Handle, p_Value, 1);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %d!", RegAddr, Error);

        return Error;
    }

    return ESP_OK;
}

/** @brief              Write multiple registers to PCA9633 using auto-increment.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      Starting register address
 *  @param p_Data       Pointer to data buffer
 *  @param Length       Number of bytes to write
 *  @return             ESP_OK on success
 */
static esp_err_t pca9633_write_registers(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, const uint8_t *p_Data, uint8_t Length)
{
    uint8_t Buffer[16];

    if (Length > 15) {
        ESP_LOGE(TAG, "Write length exceeds buffer size!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Set auto-increment flag */
    Buffer[0] = RegAddr | PCA9633_AUTO_INCREMENT;
    for (uint8_t i = 0; i < Length; i++) {
        Buffer[i + 1] = p_Data[i];
    }

    return I2CM_Write(p_Dev_Handle, Buffer, Length + 1);
}

esp_err_t PCA9633DP1_Init(i2c_master_bus_handle_t *p_Bus_Handle, i2c_master_dev_handle_t *p_Dev_Handle)
{
    esp_err_t Error;
    uint8_t PWM_Values[4] = {0, 0, 0, 0};

    if ((p_Bus_Handle == NULL) || (p_Dev_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Add device to bus */
    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_Device_I2C_Config, p_Dev_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d!", Error);

        return Error;
    }

    /* Configure MODE1: Normal mode, enable All Call address */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_MODE1, PCA9633_MODE1_ALLCALL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MODE1: %d!", Error);

        i2c_master_bus_rm_device(*p_Dev_Handle);

        return Error;
    }

    /* Wait for oscillator to start (500μs typical) */
    vTaskDelay(1 / portTICK_PERIOD_MS);

    /* Configure MODE2: Totem pole outputs, change on STOP command */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_MODE2, PCA9633_MODE2_OUTDRV);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MODE2: %d!", Error);

        i2c_master_bus_rm_device(*p_Dev_Handle);

        return Error;
    }

    /* Set all LEDs to off state */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_LEDOUT, 0x00);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDOUT: %d!", Error);

        i2c_master_bus_rm_device(*p_Dev_Handle);

        return Error;
    }

    /* Set all PWM registers to 0 (LEDs off) */
    Error = pca9633_write_registers(p_Dev_Handle, PCA9633_REG_PWM0, PWM_Values, 4);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PWM values: %d!", Error);

        i2c_master_bus_rm_device(*p_Dev_Handle);

        return Error;
    }

    ESP_LOGI(TAG, "PCA9633DP1 initialized successfully");

    return ESP_OK;
}

esp_err_t PCA9633DP1_Deinit(i2c_master_dev_handle_t *p_Dev_Handle)
{
    esp_err_t Error;

    if (p_Dev_Handle == NULL) {
        ESP_LOGE(TAG, "Invalid device handle!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Turn off all LEDs before deinit */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_LEDOUT, 0x00);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn off LEDs: %d", Error);
    }

    /* Remove device from bus */
    Error = i2c_master_bus_rm_device(*p_Dev_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);

        return Error;
    }

    ESP_LOGI(TAG, "PCA9633DP1 deinitialized");

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetLED(i2c_master_dev_handle_t *p_Dev_Handle, PCA9633_LED_t LED, uint8_t Brightness)
{
    uint8_t RegAddr;
    uint8_t LEDOUT_Value;
    esp_err_t Error;

    if (p_Dev_Handle == NULL) {
        ESP_LOGE(TAG, "Invalid device handle!");

        return ESP_ERR_INVALID_ARG;
    } else if (LED > PCA9633_LED3) {
        ESP_LOGE(TAG, "Invalid LED index!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Determine PWM register address */
    RegAddr = PCA9633_REG_PWM0 + static_cast<uint8_t>(LED);

    /* Write brightness value (0-255) */
    Error = pca9633_write_register(p_Dev_Handle, RegAddr, Brightness);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED%d brightness: %d!", LED, Error);

        return Error;
    }

    /* Read current LEDOUT configuration */
    Error = pca9633_read_register(p_Dev_Handle, PCA9633_REG_LEDOUT, &LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read LEDOUT: %d!", Error);

        return Error;
    }

    /* Set LED to PWM control mode */
    LEDOUT_Value &= ~(0x03 << (LED * 2));                   /* Clear bits for this LED */
    LEDOUT_Value |= (PCA9633_LEDOUT_PWM << (LED * 2));     /* Set to PWM mode */

    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetAllLEDs(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t LED0, uint8_t LED1, uint8_t LED2, uint8_t LED3)
{
    uint8_t PWM_Values[4];
    uint8_t LEDOUT_Value;
    esp_err_t Error;

    if (p_Dev_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Prepare PWM values */
    PWM_Values[0] = LED0;
    PWM_Values[1] = LED1;
    PWM_Values[2] = LED2;
    PWM_Values[3] = LED3;

    /* Write all PWM values using auto-increment */
    Error = pca9633_write_registers(p_Dev_Handle, PCA9633_REG_PWM0, PWM_Values, 4);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set all LED brightness: %d!", Error);

        return Error;
    }

    /* Set all LEDs to PWM control mode */
    LEDOUT_Value = (PCA9633_LEDOUT_PWM << 0) |     /* LED0 */
                   (PCA9633_LEDOUT_PWM << 2) |     /* LED1 */
                   (PCA9633_LEDOUT_PWM << 4) |     /* LED2 */
                   (PCA9633_LEDOUT_PWM << 6);      /* LED3 */

    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetLEDState(i2c_master_dev_handle_t *p_Dev_Handle, PCA9633_LED_t LED, PCA9633_LED_State_t State)
{
    uint8_t LEDOUT_Value;
    esp_err_t Error;

    if (p_Dev_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (LED > PCA9633_LED3) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read current LEDOUT configuration */
    Error = pca9633_read_register(p_Dev_Handle, PCA9633_REG_LEDOUT, &LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read LEDOUT: %d!", Error);

        return Error;
    }

    /* Update LED state */
    LEDOUT_Value &= ~(0x03 << (LED * 2));       /* Clear bits for this LED */
    LEDOUT_Value |= (State << (LED * 2));       /* Set new state */

    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetGroupControl(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t GroupPWM, uint8_t GroupFreq, bool Blinking)
{
    esp_err_t Error;
    uint8_t MODE2_Value;

    if (p_Dev_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Set group PWM value (0-255 for dimming, or duty cycle for blinking) */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_GRPPWM, GroupPWM);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GRPPWM: %d!", Error);

        return Error;
    }

    /* Set group frequency (only used in blinking mode) */
    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_GRPFREQ, GroupFreq);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GRPFREQ: %d!", Error);

        return Error;
    }

    /* Configure MODE2 for blinking or dimming */
    Error = pca9633_read_register(p_Dev_Handle, PCA9633_REG_MODE2, &MODE2_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE2: %d!", Error);

        return Error;
    }

    if (Blinking) {
        MODE2_Value |= PCA9633_MODE2_DMBLNK;
    } else {
        MODE2_Value &= ~PCA9633_MODE2_DMBLNK;
    }

    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_MODE2, MODE2_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update MODE2: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetSleepMode(i2c_master_dev_handle_t *p_Dev_Handle, bool Sleep)
{
    uint8_t MODE1_Value;
    esp_err_t Error;

    if (p_Dev_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read current MODE1 value */
    Error = pca9633_read_register(p_Dev_Handle, PCA9633_REG_MODE1, &MODE1_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE1: %d!", Error);

        return Error;
    }

    /* Update sleep bit */
    if (Sleep) {
        MODE1_Value |= PCA9633_MODE1_SLEEP;
    } else {
        MODE1_Value &= ~PCA9633_MODE1_SLEEP;
    }

    Error = pca9633_write_register(p_Dev_Handle, PCA9633_REG_MODE1, MODE1_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update MODE1: %d!", Error);

        return Error;
    }

    /* Wait for oscillator to stabilize when waking up */
    if (Sleep == false) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    return ESP_OK;
}