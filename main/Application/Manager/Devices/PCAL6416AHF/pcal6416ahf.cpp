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

#define ADDR_PCAL6416AHF_MAINBOARD          0x20
#define ADDR_PCAL6416AHF_DISPLAYBOARD       0x21

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

/** @brief Port expander port number.
 */
typedef enum {
    PORT_0          = 0x00,
    PORT_1          = 0x01,
} PortDefinition_t;

/** @brief I/O direction for a port expander pin.
 */
typedef enum {
    IO_DIR_INPUT    = 0x00,
    IO_DIR_OUTPUT   = 0x01,
} PCAL6416_Dir_t;

/** @brief Pull-resistor configuration for a port expander pin.
 */
typedef enum {
    IO_PULL_NONE    = 0x00,
    IO_PULL_UP      = 0x01,
    IO_PULL_DOWN    = 0x02,
} PCAL6416_Pull_t;

/** @brief Configuration for a single I/O pin of the PCAL6416AHF.
 */
typedef struct {
    PortDefinition_t Port;      /**< Port number (PORT_0 or PORT_1). */
    uint8_t Pin;                /**< Pin number within the port (0-7). */
    PCAL6416_Dir_t Direction;   /**< Input or output. */
    PCAL6416_Pull_t Pull;       /**< Pull-up, pull-down, or none. */
    bool isInverted;            /**< Active-low signal indicator.
                                     Inputs: polarity inversion register is set.
                                     Outputs: the API caller must negate the logic level. */
    bool isLatched;             /**< Input latch enable.
                                     When true, the input value is latched on the falling edge
                                     of the interrupt and held stable until the INPUT register
                                     is read.  Use for mechanically bouncing signals such as
                                     card-detect switches to prevent spurious interrupts. */
} PCAL6416_IO_Conf_t;

/** @brief Battery alert pin: active low, pull-up, input (PORT 0, PIN 0).
 */
#define BATTERY_ALERT_PORT          PORT_0
#define BATTERY_ALERT_PIN           0

/** @brief Battery charge pin: active high, input (PORT 0, PIN 1).
 */
#define BATTERY_CHARGE_PORT         PORT_0
#define BATTERY_CHARGE_PIN          1

/** @brief Lepton power pin: active high, output (PORT 0, PIN 2).
 */
#define LEPTON_POWER_PORT           PORT_0
#define LEPTON_POWER_PIN            2

/** @brief RTC interrupt pin: active low, pull-up, input (PORT 0, PIN 5).
 */
#define RTC_INT_PORT                PORT_0
#define RTC_INT_PIN                 5

/** @brief Temperature sensor interrupt pin: active low, pull-up, input (PORT 0, PIN 7).
 */
#define TEMP_INT_PORT               PORT_0
#define TEMP_INT_PIN                7

/** @brief Range sensor interrupt pin: active low, pull-up, input (PORT 1, PIN 0).
 *  @note  Corresponds to linear pin index 8 relative to PORT 0.
 */
#define RANGE_INT_PORT              PORT_1
#define RANGE_INT_PIN               0

/** @brief SD-card detect pin: active low, pull-up, input (PORT 1, PIN 4).
 */
#define SD_DETECT_PORT              PORT_1
#define SD_DETECT_PIN               4

/** @brief Lepton reset pin: active low output (PORT 1, PIN 5).
 */
#define LEPTON_RESET_PORT           PORT_1
#define LEPTON_RESET_PIN            5

/** @brief Camera reset pin: active low output (PORT 1, PIN 6).
 */
#define CAMERA_RESET_PORT           PORT_1
#define CAMERA_RESET_PIN            6

/** @brief Camera power pin: active high output (PORT 1, PIN 7).
 */
#define CAMERA_POWER_PORT           PORT_1
#define CAMERA_POWER_PIN            7

/** @brief Mainboard port expander pin configuration.
 *         Processed by PCAL6416AHF_Init() to derive all register values
 *         (direction, polarity, pull resistors, input latch) from a single source of truth.
 *
 *         Computed register values:
 *           CONF0      = 0xFB (P0.2 output; all others input)
 *           CONF1      = 0x1F (P1.5, P1.6, P1.7 output; all others input)
 *           POL0       = 0x01 (P0.0 Battery Alert inverted)
 *           POL1       = 0x00 (No polarity inversion on Port 1)
 *           PULL_EN0   = 0xA1 (P0.0, P0.5, P0.7)
 *           PULL_EN1   = 0x11 (P1.0, P1.4)
 *           PULL_SEL0  = 0xA1 (All pull-up)
 *           PULL_SEL1  = 0x11 (All pull-up)
 *           LATCH0     = 0x00 (No latch on Port 0)
 *           LATCH1     = 0x10 (P1.4 SD-Card Detect latched)
 */
static const PCAL6416_IO_Conf_t _PCAL6416AHF_PinConfig[] = {
    /* Port 0 */
    { PORT_0, BATTERY_ALERT_PIN,  IO_DIR_INPUT,  IO_PULL_UP,   true,  false }, /* P0.0  Battery Alert, active low, pull-up     */
    { PORT_0, BATTERY_CHARGE_PIN, IO_DIR_INPUT,  IO_PULL_NONE, false, false }, /* P0.1  Battery Charge, active high            */
    { PORT_0, LEPTON_POWER_PIN,   IO_DIR_OUTPUT, IO_PULL_NONE, false, false }, /* P0.2  Lepton Power, active high              */
    { PORT_0, RTC_INT_PIN,        IO_DIR_INPUT,  IO_PULL_UP,   false, false }, /* P0.5  RTC Interrupt, active low, pull-up     */
    { PORT_0, TEMP_INT_PIN,       IO_DIR_INPUT,  IO_PULL_UP,   false, false }, /* P0.7  Temp Interrupt, active low, pull-up    */

    /* Port 1 */
    { PORT_1, RANGE_INT_PIN,      IO_DIR_INPUT,  IO_PULL_UP,   false, false }, /* P1.0  Range Interrupt, active low, pull-up   */
    { PORT_1, SD_DETECT_PIN,      IO_DIR_INPUT,  IO_PULL_UP,   false, true  }, /* P1.4  SD-Card Detect, active low, latched    */
    { PORT_1, LEPTON_RESET_PIN,   IO_DIR_OUTPUT, IO_PULL_NONE, true,  false }, /* P1.5  Lepton Reset, active low, inverted     */
    { PORT_1, CAMERA_RESET_PIN,   IO_DIR_OUTPUT, IO_PULL_NONE, true,  false }, /* P1.6  Camera Reset, active low, inverted     */
    { PORT_1, CAMERA_POWER_PIN,   IO_DIR_OUTPUT, IO_PULL_NONE, false, false }, /* P1.7  Camera Power, active high              */
};

static const i2c_device_config_t _PCAL6416AHF_Mainboard_ExpanderConfig = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR_PCAL6416AHF_MAINBOARD,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const i2c_device_config_t _PCAL6416AHF_Displayboard_ExpanderConfig = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR_PCAL6416AHF_DISPLAYBOARD,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const char *TAG                      = "PortExpander";

/** @brief              Set the pin level of the pins of a given port.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param Port         Target port
 *  @param Mask         Pin mask
 *  @param Level        Pin level
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
static esp_err_t PCAL6416AHF_SetPinLevel(i2c_master_dev_handle_t *p_Dev_Handle, PortDefinition_t Port, uint8_t Mask,
                                         uint8_t Level)
{
    return I2CM_ModifyRegister(p_Dev_Handle, PORT_EXPANDER_REG_OUTPUT0 + static_cast<uint8_t>(Port), Mask, Level);
}

/** @brief              Read the logical level of a single input pin.
 *                      Reads the INPUT register of the specified port and extracts the bit at
 *                      the given pin position. The value reflects hardware polarity inversion
 *                      if it was configured during init.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param Port         Target port
 *  @param Pin          Pin number within the port (0-7)
 *  @param p_Level      Pointer to store the pin level (true = high, false = low)
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
static esp_err_t PCAL6416AHF_ReadPin(i2c_master_dev_handle_t *p_Dev_Handle, PortDefinition_t Port, uint8_t Pin,
                                     bool *p_Level)
{
    esp_err_t Error;
    uint8_t Temp;

    Temp = static_cast<uint8_t>(PORT_EXPANDER_REG_INPUT0 + static_cast<uint8_t>(Port));

    Error = I2CM_Write(p_Dev_Handle, &Temp, sizeof(Temp));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(p_Dev_Handle, &Temp, sizeof(Temp));
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Level = ((Temp >> Pin) & 0x01) != 0;

    return ESP_OK;
}

/** @brief              Iterates over the pin configuration table and computes the register values
 *                      for direction (CONF), polarity (POL), and pull resistors (PULL_EN / PULL_SEL),
 *                      then writes them to the device.
 *                      Polarity inversion is only applied to input pins; for outputs the
 *                      isInverted flag is metadata for the API caller.
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
    uint8_t IntMask[2] = { 0xFF, 0xFF };    /* All interrupts masked — cleared for each input pin */
    uint8_t Latch[2] = { 0x00, 0x00 };      /* Input latch disabled by default */

    for (size_t i = 0; i < Count; i++) {
        uint8_t Bit = static_cast<uint8_t>(0x01u << p_Config[i].Pin);

        if (p_Config[i].Direction == IO_DIR_OUTPUT) {
            Conf[static_cast<uint8_t>(p_Config[i].Port)] &= ~Bit;
        }

        if (p_Config[i].Direction == IO_DIR_INPUT) {
            /* Enable interrupt for this input pin (INT_MASK bit = 0 means enabled) */
            IntMask[static_cast<uint8_t>(p_Config[i].Port)] &= ~Bit;
        }

        /* Input latch: when enabled the input value is captured at the interrupt edge
         * and held until the INPUT register is read, preventing metastable reads on
         * mechanically bouncing signals (e.g. card-detect switches). */
        if ((p_Config[i].isLatched) && (p_Config[i].Direction == IO_DIR_INPUT)) {
            Latch[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;
        }

        /* Polarity inversion register only affects input pins on PCAL6416AHF */
        if ((p_Config[i].isInverted) && (p_Config[i].Direction == IO_DIR_INPUT)) {
            Pol[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;
        }

        if (p_Config[i].Pull != IO_PULL_NONE) {
            PullEn[static_cast<uint8_t>(p_Config[i].Port)] |= Bit;

            if (p_Config[i].Pull == IO_PULL_UP) {
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

esp_err_t PCAL6416AHF_ReadInterruptStatus(PCAL6416AHF_Dev_t *p_Device, PCAL6416AHF_InterruptStatus_t *p_Status)
{
    esp_err_t Error;
    uint8_t Status[2];

    if ((p_Device == NULL) || (p_Status == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Status[0] = PORT_EXPANDER_REG_INT_STATUS0;
    Error = I2CM_Write(&p_Device->Handle, &Status[0], 1);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, Status, 2);
    if (Error != ESP_OK) {
        return Error;
    }

    p_Status->BatteryAlert = ((Status[0] >> BATTERY_ALERT_PIN) & 0x01) != 0;
    p_Status->BatteryCharging = ((Status[0] >> BATTERY_CHARGE_PIN) & 0x01) != 0;
    p_Status->RTCInterrupt = ((Status[0] >> RTC_INT_PIN) & 0x01) != 0;
    p_Status->TempInterrupt = ((Status[0] >> TEMP_INT_PIN) & 0x01) != 0;
    p_Status->RangeInterrupt = ((Status[1] >> RANGE_INT_PIN) & 0x01) != 0;
    p_Status->SDDetect = ((Status[1] >> SD_DETECT_PIN) & 0x01) != 0;

    Status[0] = PORT_EXPANDER_REG_INPUT0;

    I2CM_Write(&p_Device->Handle, &Status[0], 1);
    I2CM_Read(&p_Device->Handle, Status, 2);

    p_Status->BatteryAlertLevel = ((Status[0] >> BATTERY_ALERT_PIN) & 0x01) != 0;
    p_Status->BatteryChargingLevel = ((Status[0] >> BATTERY_CHARGE_PIN) & 0x01) != 0;
    p_Status->SDDetectInserted = ((Status[1] >> SD_DETECT_PIN) & 0x01) == 0;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_Init(i2c_master_bus_handle_t *p_Bus_Handle, PCAL6416AHF_Dev_t *p_Device)
{
    esp_err_t Error;
    uint8_t Temp[2];

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_PCAL6416AHF_Mainboard_ExpanderConfig, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d!", Error);

        return Error;
    }

    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "Configure Port Expander...");

    Error = PCAL6416AHF_Apply_PinConfig(&p_Device->Handle, _PCAL6416AHF_PinConfig,
                                        sizeof(_PCAL6416AHF_PinConfig) / sizeof(_PCAL6416AHF_PinConfig[0]));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply pin configuration: %d!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    Temp[0] = PORT_EXPANDER_REG_INT_STATUS0;
    I2CM_Write(&p_Device->Handle, &Temp[0], 1);
    I2CM_Read(&p_Device->Handle, Temp, 2);

    Temp[0] = PORT_EXPANDER_REG_INPUT0;
    I2CM_Write(&p_Device->Handle, &Temp[0], 1);
    I2CM_Read(&p_Device->Handle, Temp, 2);

    return ESP_OK;
}

esp_err_t PCAL6416AHF_Deinit(PCAL6416AHF_Dev_t *p_Device)
{
    if (p_Device->Handle != NULL) {
        esp_err_t Error;

        Error = i2c_master_bus_rm_device(p_Device->Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);

            return Error;
        }

        p_Device->Handle = NULL;
    }

    return ESP_OK;
}

esp_err_t PCAL6416AHF_EnableCamera(PCAL6416AHF_Dev_t *p_Device, bool Enable)
{
    return PCAL6416AHF_SetPinLevel(&p_Device->Handle, CAMERA_POWER_PORT,
                                   static_cast<uint8_t>(0x01u << CAMERA_POWER_PIN),
                                   static_cast<uint8_t>(static_cast<uint8_t>(Enable) << CAMERA_POWER_PIN));
}

esp_err_t PCAL6416AHF_EnableLED(PCAL6416AHF_Dev_t *p_Device, bool Enable)
{
    ESP_LOGW(TAG, "No LED pin assigned to this port expander!");

    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t PCAL6416AHF_EnableLeptonReset(PCAL6416AHF_Dev_t *p_Device, bool Reset)
{
    return PCAL6416AHF_SetPinLevel(&p_Device->Handle, LEPTON_RESET_PORT, (0x01 << LEPTON_RESET_PIN),
                                   ((Reset == false ? 1 : 0) << LEPTON_RESET_PIN));
}

esp_err_t PCAL6416AHF_SetLeptonPower(PCAL6416AHF_Dev_t *p_Device, bool Enable)
{
    return PCAL6416AHF_SetPinLevel(&p_Device->Handle, LEPTON_POWER_PORT,
                                   static_cast<uint8_t>(0x01 << LEPTON_POWER_PIN),
                                   static_cast<uint8_t>(static_cast<uint8_t>(Enable) << LEPTON_POWER_PIN));
}

esp_err_t PCAL6416AHF_SetCameraReset(PCAL6416AHF_Dev_t *p_Device, bool Reset)
{
    return PCAL6416AHF_SetPinLevel(&p_Device->Handle, CAMERA_RESET_PORT,
                                   static_cast<uint8_t>(0x01 << CAMERA_RESET_PIN),
                                   static_cast<uint8_t>((Reset == false ? 1 : 0) << CAMERA_RESET_PIN));
}

esp_err_t PCAL6416AHF_GetBatteryAlert(PCAL6416AHF_Dev_t *p_Device, bool *p_Alert)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Alert == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Polarity inversion is set in hardware for this pin. Bit = 1 means alert is active */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, BATTERY_ALERT_PORT, BATTERY_ALERT_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Alert = Level;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_GetBatteryCharging(PCAL6416AHF_Dev_t *p_Device, bool *p_Charging)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Charging == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Active high. Bit = 1 means charging */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, BATTERY_CHARGE_PORT, BATTERY_CHARGE_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Charging = Level;

    return ESP_OK;
}

esp_err_t PCAL6416AHF_GetRTCInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Triggered == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Active low - pin goes low when triggered and no HW polarity inversion. Bit = 0 means triggered */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, RTC_INT_PORT, RTC_INT_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t PCAL6416AHF_GetTempInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Triggered == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Active low - pin goes low when triggered and no HW polarity inversion. Bit = 0 means triggered */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, TEMP_INT_PORT, TEMP_INT_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t PCAL6416AHF_GetRangeInterrupt(PCAL6416AHF_Dev_t *p_Device, bool *p_Triggered)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Triggered == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Active low - pin goes low when triggered and no HW polarity inversion. Bit = 0 means triggered */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, RANGE_INT_PORT, RANGE_INT_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t PCAL6416AHF_GetSDDetect(PCAL6416AHF_Dev_t *p_Device, bool *p_Inserted)
{
    esp_err_t Error;
    bool Level;

    if ((p_Device == NULL) || (p_Inserted == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Active low - SD card present pulls pin low and no HW polarity inversion. Bit = 0 means inserted */
    Error = PCAL6416AHF_ReadPin(&p_Device->Handle, SD_DETECT_PORT, SD_DETECT_PIN, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Inserted = (Level == false);

    return ESP_OK;
}