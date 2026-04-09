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
#define PCA9633_MODE1_AI2                   (1 << 7)    /**< Auto-Increment: All registers. */
#define PCA9633_MODE1_AI1                   (1 << 6)    /**< Auto-Increment: Individual brightness only. */
#define PCA9633_MODE1_AI0                   (1 << 5)    /**< Auto-Increment: Global control only. */
#define PCA9633_MODE1_SLEEP                 (1 << 4)    /**< Low power mode (oscillator off). */
#define PCA9633_MODE1_SUB1                  (1 << 3)    /**< Respond to subaddress 1. */
#define PCA9633_MODE1_SUB2                  (1 << 2)    /**< Respond to subaddress 2. */
#define PCA9633_MODE1_SUB3                  (1 << 1)    /**< Respond to subaddress 3. */
#define PCA9633_MODE1_ALLCALL               (1 << 0)    /**< Respond to All Call I2C-bus address. */

/** @brief MODE2 register bit masks.
 */
#define PCA9633_MODE2_DMBLNK                (1 << 5)    /**< Group control: blinking (1) or dimming (0). */
#define PCA9633_MODE2_INVRT                 (1 << 4)    /**< Output logic state inverted. */
#define PCA9633_MODE2_OCH                   (1 << 3)    /**< Outputs change on ACK (0) or STOP (1). */
#define PCA9633_MODE2_OUTDRV                (1 << 2)    /**< Output driver: totem pole (1) or open-drain (0). */
#define PCA9633_MODE2_OUTNE1                (1 << 1)    /**< Output state when OE = 1 (bit 1). */
#define PCA9633_MODE2_OUTNE0                (1 << 0)    /**< Output state when OE = 1 (bit 0). */

/** @brief LEDOUT register - LED driver output state.
 */
#define PCA9633_LEDOUT_OFF                  0x00        /**< LED driver off. */
#define PCA9633_LEDOUT_ON                   0x01        /**< LED driver fully on (not PWM controlled). */
#define PCA9633_LEDOUT_PWM                  0x02        /**< LED driver individual PWM control. */
#define PCA9633_LEDOUT_GRPPWM               0x03        /**< LED driver group PWM control. */

static const i2c_device_config_t _PCA9633DP1_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = PCA9633_I2C_ADDR,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const char *TAG = "PCA9633DP1";

/** @brief              Write a single byte to a PCA9633 register (no auto-increment).
 *                      Control byte format: [0|0|0|0|A3|A2|A1|A0] (bits [7:5] = 0).
 *  @param p_Dev_Handle Pointer to device handle
 *  @param RegAddr      Register address (0x00–0x0C)
 *  @param Value        Byte to write
 *  @return             ESP_OK on success, error code otherwise
 */
static esp_err_t PCA9633_Write_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint8_t Value)
{
    uint8_t Buffer[2] = { RegAddr, Value };

    return static_cast<esp_err_t>(I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer)));
}

/** @brief              Write multiple consecutive PCA9633 registers using auto-increment.
 *                      Control byte bit 7 (AI2) = 1 selects auto-increment through all
 *                      13 registers (datasheet Table 7: AI2 = 1, AI1 = 0, AI0 = 0).
 *  @param p_Dev_Handle Pointer to device handle
 *  @param StartReg     First register address (0x00–0x0C)
 *  @param p_Data       Pointer to data buffer
 *  @param Length       Number of registers to write (1–13)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if Length is 0 or > 13
 *                      ESP_FAIL on I2C error
 */
static esp_err_t PCA9633_Write_Registers(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t StartReg,
                                         const uint8_t *p_Data, uint8_t Length)
{
    uint8_t Buffer[14];

    if ((Length == 0) || (Length > 13)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Control byte: AI2=1 (bit 7) enables auto-increment through all registers */
    Buffer[0] = 0x80 | StartReg;
    for (uint8_t i = 0; i < Length; i++) {
        Buffer[i + 1] = p_Data[i];
    }

    return static_cast<esp_err_t>(I2CM_Write(p_Dev_Handle, Buffer, static_cast<uint32_t>(Length + 1)));
}

/** @brief              Read a single byte from a PCA9633 register (no auto-increment).
 *                      Sends the plain register address as control byte, then reads 1 byte.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param RegAddr      Register address (0x00–0x0C)
 *  @param p_Value      Pointer to store the read byte
 *  @return             ESP_OK on success, error code otherwise
 */
static esp_err_t PCA9633_Read_Register(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t RegAddr, uint8_t *p_Value)
{
    esp_err_t Error;

    Error = static_cast<esp_err_t>(I2CM_Write(p_Dev_Handle, &RegAddr, 1));
    if (Error != ESP_OK) {
        return Error;
    }

    return static_cast<esp_err_t>(I2CM_Read(p_Dev_Handle, p_Value, 1));
}

esp_err_t PCA9633DP1_Init(i2c_master_bus_handle_t *p_Bus_Handle, PCA9633DP1_Dev_t *p_Device)
{
    esp_err_t Error;
    uint8_t Zeros[4] = { 0, 0, 0, 0 };

    if ((p_Bus_Handle == NULL) || (p_Device == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_PCA9633DP1_I2C_Config, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d!", Error);

        return Error;
    }

    /* Configure MODE2 while chip is still in power-on SLEEP (oscillator off).
     * Totem-pole (OUTDRV = 1), no inversion (INVRT = 0).
     * Reason INVRT must NOT be set for NPN transistors with totem-pole:
     *   INVRT = 0, LEDOUT = PWM, PWM = 0 -> output LOW -> NPN base at 0 V -> transistor OFF
     *   INVRT = 1, LEDOUT = PWM, PWM = 0 -> output HIGH (inverted) -> NPN turns ON
     *   LEDOUT = 0 (off-state) = High-Z regardless of INVRT; NPN may float ON */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_MODE2, PCA9633_MODE2_OUTDRV | PCA9633_MODE2_INVRT);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MODE2: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Keep oscillator in sleep; enable ALLCALL address. */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_MODE1,
                                   PCA9633_MODE1_SLEEP | PCA9633_MODE1_ALLCALL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MODE1 (sleep): %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Set all four channels to individual PWM mode (LEDOUT = 0xAA).
     * LEDOUT_OFF (0x00) puts outputs in High-Z; a floating NPN base with any
     * pull-up would allow the transistor to turn on.
     * LEDOUT_PWM (0x02) with a 0% duty cycle actively drives outputs LOW via
     * totem-pole, holding all NPN bases at 0 V -> transistors OFF -> LEDs OFF. */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_LEDOUT,
                                   static_cast<uint8_t>((PCA9633_LEDOUT_PWM << 0) |
                                                        (PCA9633_LEDOUT_PWM << 2) |
                                                        (PCA9633_LEDOUT_PWM << 4) |
                                                        (PCA9633_LEDOUT_PWM << 6)));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDOUT: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Set all PWM duty cycles to 0 % (output LOW -> NPN off -> LED off). */
    Error = PCA9633_Write_Registers(&p_Device->Handle, PCA9633_REG_PWM0, Zeros, 4);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PWM values: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Wake oscillator; outputs immediately driven LOW (PWM=0 %, totem-pole). */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_MODE1, PCA9633_MODE1_ALLCALL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake PCA9633DP1: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Oscillator startup time: 500 µs typical (datasheet §7.3.3). */
    vTaskDelay(pdMS_TO_TICKS(1));

    ESP_LOGD(TAG, "PCA9633DP1 initialized successfully");

    return ESP_OK;
}

esp_err_t PCA9633DP1_Deinit(PCA9633DP1_Dev_t *p_Device)
{
    uint8_t Zeros[4] = { 0, 0, 0, 0 };

    esp_err_t Error = ESP_OK;

    if (p_Device == NULL) {
        ESP_LOGE(TAG, "Invalid device handle!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Set all PWM duty cycles to 0 (output LOW -> NPN off) before removing device */
    Error = PCA9633_Write_Registers(&p_Device->Handle, PCA9633_REG_PWM0, Zeros, 4);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn off LEDs: %d", Error);
    }

    /* Remove device from bus */
    Error = i2c_master_bus_rm_device(p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "PCA9633DP1 deinitialized");

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetLED(PCA9633DP1_Dev_t *p_Device, PCA9633_LED_t LED, uint8_t Brightness)
{
    uint8_t RegAddr;
    uint8_t LEDOUT_Value;
    esp_err_t Error = ESP_OK;

    if (p_Device == NULL) {
        ESP_LOGE(TAG, "Invalid device handle!");

        return ESP_ERR_INVALID_ARG;
    } else if (LED > PCA9633_LED3) {
        ESP_LOGE(TAG, "Invalid LED index!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Determine PWM register address */
    RegAddr = PCA9633_REG_PWM0 + static_cast<uint8_t>(LED);

    /* Write brightness: PWM = 0 -> output LOW -> NPN off; PWM = 255 -> output HIGH -> NPN on */
    Error = PCA9633_Write_Register(&p_Device->Handle, RegAddr, Brightness);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED%d brightness: %d!", static_cast<int>(LED), Error);

        return Error;
    }

    /* Read current LEDOUT configuration */
    Error = PCA9633_Read_Register(&p_Device->Handle, PCA9633_REG_LEDOUT, &LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read LEDOUT: %d!", Error);

        return Error;
    }

    /* Set channel to individual PWM mode (actively driven LOW at PWM = 0, HIGH at PWM = 255) */
    LEDOUT_Value &= static_cast<uint8_t>(~(0x03 << (static_cast<int>(LED) * 2)));
    LEDOUT_Value |= static_cast<uint8_t>(PCA9633_LEDOUT_PWM << (static_cast<int>(LED) * 2));

    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetAllLEDs(PCA9633DP1_Dev_t *p_Device, uint8_t LED0, uint8_t LED1, uint8_t LED2,
                                uint8_t LED3)
{
    uint8_t PWM_Values[4];
    uint8_t LEDOUT_Value;
    esp_err_t Error = ESP_OK;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Prepare PWM values */
    PWM_Values[0] = LED0;
    PWM_Values[1] = LED1;
    PWM_Values[2] = LED2;
    PWM_Values[3] = LED3;

    /* Write all four PWM values in one auto-increment burst */
    Error = PCA9633_Write_Registers(&p_Device->Handle, PCA9633_REG_PWM0, PWM_Values, 4);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set all LED brightness: %d!", Error);

        return Error;
    }

    /* Set all channels to individual PWM mode */
    LEDOUT_Value = static_cast<uint8_t>((PCA9633_LEDOUT_PWM << 0) |
                                        (PCA9633_LEDOUT_PWM << 2) |
                                        (PCA9633_LEDOUT_PWM << 4) |
                                        (PCA9633_LEDOUT_PWM << 6));

    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetLEDState(PCA9633DP1_Dev_t *p_Device, PCA9633_LED_t LED, PCA9633_LED_State_t State)
{
    uint8_t LEDOUT_Value;
    esp_err_t Error = ESP_OK;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (LED > PCA9633_LED3) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read current LEDOUT configuration */
    Error = PCA9633_Read_Register(&p_Device->Handle, PCA9633_REG_LEDOUT, &LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read LEDOUT: %d!", Error);

        return Error;
    }

    /* Update the two-bit state field for this channel */
    LEDOUT_Value &= static_cast<uint8_t>(~(0x03 << (static_cast<int>(LED) * 2)));
    LEDOUT_Value |= static_cast<uint8_t>(static_cast<uint8_t>(State) << (static_cast<int>(LED) * 2));

    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_LEDOUT, LEDOUT_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDOUT: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetGroupControl(PCA9633DP1_Dev_t *p_Device, uint8_t GroupPWM, uint8_t GroupFreq,
                                     bool Blinking)
{
    esp_err_t Error = ESP_OK;
    uint8_t MODE2_Value;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Set group PWM value (0–255: dimming intensity or blinking duty cycle) */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_GRPPWM, GroupPWM);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GRPPWM: %d!", Error);

        return Error;
    }

    /* Set group frequency (only meaningful in blinking mode) */
    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_GRPFREQ, GroupFreq);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GRPFREQ: %d!", Error);

        return Error;
    }

    /* Read-modify-write MODE2 to set or clear the blinking flag */
    Error = PCA9633_Read_Register(&p_Device->Handle, PCA9633_REG_MODE2, &MODE2_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE2: %d!", Error);

        return Error;
    }

    if (Blinking) {
        MODE2_Value |= PCA9633_MODE2_DMBLNK;
    } else {
        MODE2_Value &= ~PCA9633_MODE2_DMBLNK;
    }

    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_MODE2, MODE2_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update MODE2: %d!", Error);

        return Error;
    }

    return ESP_OK;
}

esp_err_t PCA9633DP1_SetSleepMode(PCA9633DP1_Dev_t *p_Device, bool Sleep)
{
    uint8_t MODE1_Value;
    esp_err_t Error = ESP_OK;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read-modify-write MODE1 to set or clear the sleep bit */
    Error = PCA9633_Read_Register(&p_Device->Handle, PCA9633_REG_MODE1, &MODE1_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODE1: %d!", Error);

        return Error;
    }

    if (Sleep) {
        MODE1_Value |= PCA9633_MODE1_SLEEP;
    } else {
        MODE1_Value &= ~PCA9633_MODE1_SLEEP;
    }

    Error = PCA9633_Write_Register(&p_Device->Handle, PCA9633_REG_MODE1, MODE1_Value);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update MODE1: %d!", Error);

        return Error;
    }

    /* Wait for oscillator to stabilize when waking up */
    if (Sleep == false) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return ESP_OK;
}