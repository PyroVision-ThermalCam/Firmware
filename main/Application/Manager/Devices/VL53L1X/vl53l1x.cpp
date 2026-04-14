/*
 * vl53l1x.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VL53L1CXV0FY1 Time-of-Flight distance sensor driver implementation.
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

#include "vl53l1x.h"

#include <sdkconfig.h>

/** @brief VL53L1X I2C address (fixed, factory programmed).
 */
#define VL53L1X_I2C_ADDR                                0x29

/** @brief VL53L1X key register addresses (16-bit addressing).
 */
#define VL53L1X_REG_SOFT_RESET                          0x0000
#define VL53L1X_REG_OSC_MEASURED_FAST_OSC_FREQUENCY     0x0006
#define VL53L1X_REG_MM_CONFIG_OUTER_OFFSET_MM           0x0022
#define VL53L1X_REG_GPIO_HV_MUX_CTRL                    0x0030
#define VL53L1X_REG_GPIO_TIO_HV_STATUS                  0x0031
#define VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR              0x0086
#define VL53L1X_REG_SYSTEM_MODE_START                   0x0087
#define VL53L1X_REG_RESULT_RANGE_STATUS                 0x0089
#define VL53L1X_REG_RESULT_SPAD_NB                      0x008C
#define VL53L1X_REG_RESULT_SIGNAL_RATE                  0x008E
#define VL53L1X_REG_RESULT_AMBIENT_RATE                 0x0090
#define VL53L1X_REG_RESULT_DISTANCE_MM                  0x0096
#define VL53L1X_REG_ALGO_PART_TO_PART_RANGE_OFFSET_MM   0x001E
#define VL53L1X_REG_MODEL_ID                            0x010F
#define VL53L1X_REG_RESULT_OSC_CALIBRATE_VAL            0x00DE

/** @brief VHV calibration and ranging init registers.
 *         Must be written before and after the initial calibration ranging cycle.
 */
#define VL53L1X_REG_VHV_CONFIG_TIMEOUT                  0x0008      /**< VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND */
#define VL53L1X_REG_INIT_RANGING                        0x000B      /**< Pad timeout override (write 0 to disable) */

/** @brief Distance mode and timing budget configuration registers.
 */
#define VL53L1X_REG_PHASECAL_TIMEOUT                    0x004B
#define VL53L1X_REG_RANGE_TIMEOUT_A_HI                  0x005E
#define VL53L1X_REG_RANGE_TIMEOUT_A_LO                  0x005F
#define VL53L1X_REG_RANGE_VCSEL_PERIOD_A                0x0060
#define VL53L1X_REG_RANGE_TIMEOUT_B_HI                  0x0061
#define VL53L1X_REG_RANGE_TIMEOUT_B_LO                  0x0062
#define VL53L1X_REG_RANGE_VCSEL_PERIOD_B                0x0063
#define VL53L1X_REG_VALID_PHASE_HIGH                    0x0069
#define VL53L1X_REG_SD_WOI_SD0                          0x008F
#define VL53L1X_REG_SD_WOI_SD1                          0x0090      /**< Note: overlaps AMBIENT_RATE; context differs */
#define VL53L1X_REG_SD_INITIAL_PHASE_SD0                0x0091      /**< Note: overlaps; used pre-start only */
#define VL53L1X_REG_SD_INITIAL_PHASE_SD1                0x0092

/** @brief Boot status register.
 *         Bit 0 = 1 after firmware has completed boot sequence.
 */
#define VL53L1X_REG_FIRMWARE_SYSTEM_STATUS              0x00E5

/** @brief Static initialization configuration register addresses.
 *         Written once during VL53L1_StaticInit() to configure default tuning parameters.
 */
#define VL53L1X_REG_DSS_CONFIG_TARGET_TOTAL_RATE_MCPS   0x0024      /**< DSS_CONFIG__TARGET_TOTAL_RATE_MCPS (16-bit). */
#define VL53L1X_REG_DSS_CONFIG_ROI_MODE_CONTROL         0x004F      /**< DSS_CONFIG__ROI_MODE_CONTROL. */
#define VL53L1X_REG_SYSTEM_THRESH_RATE_HIGH             0x0050      /**< SYSTEM__THRESH_RATE_HIGH (16-bit). */
#define VL53L1X_REG_SYSTEM_THRESH_RATE_LOW              0x0052      /**< SYSTEM__THRESH_RATE_LOW (16-bit). */
#define VL53L1X_REG_DSS_CONFIG_MANUAL_SPADS_SELECT      0x0054      /**< DSS_CONFIG__MANUAL_EFFECTIVE_SPADS_SELECT (16-bit). */
#define VL53L1X_REG_DSS_CONFIG_APERTURE_ATTENUATION     0x0057      /**< DSS_CONFIG__APERTURE_ATTENUATION. */
#define VL53L1X_REG_RANGE_CONFIG_SIGMA_THRESH           0x0064      /**< RANGE_CONFIG__SIGMA_THRESH (16-bit). */
#define VL53L1X_REG_RANGE_CONFIG_MIN_COUNT_RATE_MCPS    0x0066      /**< RANGE_CONFIG__MIN_COUNT_RATE_RTN_LIMIT_MCPS (16-bit). */
#define VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD_0         0x0071      /**< SYSTEM__GROUPED_PARAMETER_HOLD_0. */
#define VL53L1X_REG_SYSTEM_SEED_CONFIG                  0x0077      /**< SYSTEM__SEED_CONFIG. */
#define VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD_1         0x007C      /**< SYSTEM__GROUPED_PARAMETER_HOLD_1. */
#define VL53L1X_REG_SD_CONFIG_QUANTIFIER                0x007E      /**< SD_CONFIG__QUANTIFIER. */
#define VL53L1X_REG_SYSTEM_SEQUENCE_CONFIG              0x0081      /**< SYSTEM__SEQUENCE_CONFIG. */
#define VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD           0x0082      /**< SYSTEM__GROUPED_PARAMETER_HOLD. */
#define VL53L1X_REG_SIGMA_EST_PULSE_WIDTH_NS            0x0036      /**< SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS. */
#define VL53L1X_REG_SIGMA_EST_AMBIENT_WIDTH_NS          0x0037      /**< SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS. */
#define VL53L1X_REG_ALGO_CROSSTALK_VALID_HEIGHT_MM      0x0039      /**< ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM. */
#define VL53L1X_REG_ALGO_RANGE_IGNORE_VALID_HEIGHT_MM   0x003E      /**< ALGO__RANGE_IGNORE_VALID_HEIGHT_MM. */
#define VL53L1X_REG_ALGO_RANGE_MIN_CLIP                 0x003F      /**< ALGO__RANGE_MIN_CLIP. */
#define VL53L1X_REG_ALGO_CONSISTENCY_TOLERANCE          0x0040      /**< ALGO__CONSISTENCY_CHECK__TOLERANCE. */

/** @brief Continuous ranging and calibration configuration registers.
 */
#define VL53L1X_REG_SYSTEM_INTERMEASUREMENT_PERIOD      0x006C      /**< SYSTEM__INTERMEASUREMENT_PERIOD (32-bit big-endian). */
#define VL53L1X_REG_PHASECAL_CONFIG_OVERRIDE            0x004D      /**< PHASECAL_CONFIG__OVERRIDE. */
#define VL53L1X_REG_CAL_CONFIG_VCSEL_START              0x0047      /**< CAL_CONFIG__VCSEL_START. */
#define VL53L1X_REG_PHASECAL_RESULT_VCSEL_START         0x00D8      /**< PHASECAL_RESULT__VCSEL_START. */

/** @brief DSS target rate for automatic SPAD selection.
 *         20.0 Mcps expressed as a 9.7 fixed-point value (20 × 128 = 0x0A00).
 */
#define VL53L1X_TARGET_RATE_MCPS                        0x0A00

/** @brief System mode start command values.
 */
#define VL53L1X_MODE_STOP                               0x00
#define VL53L1X_MODE_CONTINUOUS                         0x40
#define VL53L1X_MODE_SINGLE_SHOT                        0x10

/** @brief Expected model ID for VL53L1CXV0FY1.
 */
#define VL53L1X_MODEL_ID_EXPECTED                       0xEA

/** @brief GPIO TIO status bit: 0 = data ready, 1 = not ready.
 */
#define VL53L1X_DATA_READY_BIT                          0x01

/** @brief Timing budget configuration per mode.
 *         Values for TIMEOUT_MACROP_A (Hi/Lo) and TIMEOUT_MACROP_B (Hi/Lo).
 */
typedef struct {
    uint16_t MacropA;                           /**< RANGE_CONFIG__TIMEOUT_MACROP_A (16-bit big-endian). */
    uint16_t MacropB;                           /**< RANGE_CONFIG__TIMEOUT_MACROP_B (16-bit big-endian). */
} VL53L1X_TimingConfig_t;

/** @brief Timing budget lookup table for Short distance mode.
 */
static const VL53L1X_TimingConfig_t _VL53L1X_Timing_Short[] = {
    { 0x001D, 0x0027 },     /* 15 ms  */
    { 0x0051, 0x006E },     /* 20 ms  */
    { 0x00D6, 0x006E },     /* 33 ms  */
    { 0x01AE, 0x01E8 },     /* 50 ms  */
    { 0x02E1, 0x0388 },     /* 100 ms */
    { 0x03E1, 0x0496 },     /* 200 ms */
    { 0x0591, 0x05C1 },     /* 500 ms */
};

/** @brief Timing budget lookup table for Long distance mode.
 *         15 ms is NOT supported in Long mode.
 */
static const VL53L1X_TimingConfig_t _VL53L1X_Timing_Long[] = {
    { 0x0000, 0x0000 },     /* 15 ms – invalid for Long mode, placeholder */
    { 0x001E, 0x0022 },     /* 20 ms  */
    { 0x0060, 0x006E },     /* 33 ms  */
    { 0x00AD, 0x00C6 },     /* 50 ms  */
    { 0x01CC, 0x01EA },     /* 100 ms */
    { 0x02D9, 0x02F8 },     /* 200 ms */
    { 0x048F, 0x04A4 },     /* 500 ms */
};

static i2c_device_config_t _VL53L1X_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = VL53L1X_I2C_ADDR,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const char *TAG = "VL53L1X";

/** @brief              Map raw hardware range status to VL53L1X_RangeStatus_t.
 *  @param RawStatus    Raw 4-bit status from RESULT__RANGE_STATUS bits [4:0]
 *  @return             Mapped VL53L1X_RangeStatus_t value
 */
static VL53L1X_RangeStatus_t VL53L1X_Map_Range_Status(uint8_t RawStatus)
{
    switch (RawStatus) {
        case 9: {
            return VL53L1X_RANGE_VALID;
        }
        case 6: {
            return VL53L1X_RANGE_SIGMA_FAIL;
        }
        case 4: {
            return VL53L1X_RANGE_SIGNAL_FAIL;
        }
        case 8: {
            return VL53L1X_RANGE_OUT_OF_BOUNDS;
        }
        case 7: {
            return VL53L1X_RANGE_WRAP_FAIL;
        }
        default: {
            return VL53L1X_RANGE_INVALID;
        }
    }
}

/** @brief Returns the index into the timing lookup tables for a given budget.
 */
static int VL53L1X_TimingBudget_ToIndex(VL53L1X_TimingBudget_t Budget)
{
    switch (Budget) {
        case VL53L1X_TIMING_15MS: {
            return 0;
        }
        case VL53L1X_TIMING_20MS: {
            return 1;
        }
        case VL53L1X_TIMING_33MS: {
            return 2;
        }
        case VL53L1X_TIMING_50MS: {
            return 3;
        }
        case VL53L1X_TIMING_100MS: {
            return 4;
        }
        case VL53L1X_TIMING_200MS: {
            return 5;
        }
        case VL53L1X_TIMING_500MS: {
            return 6;
        }
        default: {
            return -1;
        }
    }
}

/** @brief              Write a single byte to a 16-bit register address.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      16-bit register address
 *  @param Value        Byte value to write
 *  @return             ESP_OK on success
 */
static esp_err_t VL53L1X_Write_Register8(i2c_master_dev_handle_t *p_Dev_Handle, uint16_t RegAddr, uint8_t Value)
{
    uint8_t Buffer[3];

    Buffer[0] = static_cast<uint8_t>((RegAddr >> 8) & 0xFF);
    Buffer[1] = static_cast<uint8_t>(RegAddr & 0xFF);
    Buffer[2] = Value;

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Write a 16-bit big-endian value to a 16-bit register address.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      16-bit register address
 *  @param Value        16-bit value to write (sent MSB first)
 *  @return             ESP_OK on success
 */
static esp_err_t VL53L1X_Write_Register16(i2c_master_dev_handle_t *p_Dev_Handle, uint16_t RegAddr, uint16_t Value)
{
    uint8_t Buffer[4];

    Buffer[0] = static_cast<uint8_t>((RegAddr >> 8) & 0xFF);
    Buffer[1] = static_cast<uint8_t>(RegAddr & 0xFF);
    Buffer[2] = static_cast<uint8_t>((Value >> 8) & 0xFF);
    Buffer[3] = static_cast<uint8_t>(Value & 0xFF);

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Write a 32-bit big-endian value to a 16-bit register address.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      16-bit register address
 *  @param Value        32-bit value to write (sent MSB first)
 *  @return             ESP_OK on success
 */
static esp_err_t VL53L1X_Write_Register32(i2c_master_dev_handle_t *p_Dev_Handle, uint16_t RegAddr, uint32_t Value)
{
    uint8_t Buffer[6];

    Buffer[0] = static_cast<uint8_t>((RegAddr >> 8) & 0xFF);
    Buffer[1] = static_cast<uint8_t>(RegAddr & 0xFF);
    Buffer[2] = static_cast<uint8_t>((Value >> 24) & 0xFF);
    Buffer[3] = static_cast<uint8_t>((Value >> 16) & 0xFF);
    Buffer[4] = static_cast<uint8_t>((Value >> 8) & 0xFF);
    Buffer[5] = static_cast<uint8_t>(Value & 0xFF);

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Read a single byte from a 16-bit register address.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      16-bit register address
 *  @param p_Value      Pointer to store the byte
 *  @return             ESP_OK on success
 */
static esp_err_t VL53L1X_Write_Register8(i2c_master_dev_handle_t *p_Dev_Handle, uint16_t RegAddr, uint8_t *p_Value)
{
    uint8_t AddrBuffer[2];
    esp_err_t Error;

    AddrBuffer[0] = static_cast<uint8_t>((RegAddr >> 8) & 0xFF);
    AddrBuffer[1] = static_cast<uint8_t>(RegAddr & 0xFF);

    Error = I2CM_Write(p_Dev_Handle, AddrBuffer, sizeof(AddrBuffer));
    if (Error != ESP_OK) {
        return Error;
    }

    return I2CM_Read(p_Dev_Handle, p_Value, 1);
}

/** @brief              Read a 16-bit big-endian value from a 16-bit register address.
 *  @param p_Dev_Handle Device handle
 *  @param RegAddr      16-bit register address
 *  @param p_Value      Pointer to store the 16-bit value
 *  @return             ESP_OK on success
 */
static esp_err_t VL53L1X_Write_Register8(i2c_master_dev_handle_t *p_Dev_Handle, uint16_t RegAddr, uint16_t *p_Value)
{
    uint8_t AddrBuffer[2];
    uint8_t DataBuffer[2];
    esp_err_t Error;

    AddrBuffer[0] = static_cast<uint8_t>((RegAddr >> 8) & 0xFF);
    AddrBuffer[1] = static_cast<uint8_t>(RegAddr & 0xFF);

    Error = I2CM_Write(p_Dev_Handle, AddrBuffer, sizeof(AddrBuffer));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = I2CM_Read(p_Dev_Handle, DataBuffer, sizeof(DataBuffer));
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Value = (static_cast<uint16_t>(DataBuffer[0]) << 8) | static_cast<uint16_t>(DataBuffer[1]);

    return ESP_OK;
}

/** @brief              Setup manual calibration after the first ranging cycle in continuous mode.
 *                      Saves VHV_CONFIG__INIT and VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND, disables
 *                      VHV re-initialization, sets the loop bound to the tuning parameter default
 *                      (LOWPOWERAUTO_VHV_LOOP_BOUND_DEFAULT = 3), and overrides phasecal with a
 *                      static VCSEL start value. Subsequent ranging cycles skip VHV and phasecal
 *                      calibration, saving approximately 1.8 ms per measurement.
 *                      Based on VL53L1_low_power_auto_setup_manual_calibration() in the ST API.
 *  @note               Called automatically on the first VL53L1X_GetResult() call.
 *                      Stored VHV values are restored when VL53L1X_StopContinuous() is called.
 *  @param p_Dev_Handle Device handle
 *  @return             ESP_OK on success
 *                      ESP_FAIL if I2C communication fails
 */
static esp_err_t VL53L1X_Setup_Manual_Calibration(VL53L1X_Dev_t *p_Device)
{
    esp_err_t Error;
    uint8_t PhasecalVcselStart;

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_INIT_RANGING, &p_Device->SavedVhvInit);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_VHV_CONFIG_TIMEOUT, &p_Device->SavedVhvTimeout);
    if (Error != ESP_OK) {
        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_INIT_RANGING,
                                    static_cast<uint8_t>(p_Device->SavedVhvInit & 0x7F));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_VHV_CONFIG_TIMEOUT,
                                    static_cast<uint8_t>((p_Device->SavedVhvTimeout & 0x03) + (3 << 2)));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_PHASECAL_CONFIG_OVERRIDE,
                                    static_cast<uint8_t>(0x01));
    if (Error != ESP_OK) {
        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_PHASECAL_RESULT_VCSEL_START,
                                    &PhasecalVcselStart);
    if (Error != ESP_OK) {
        return Error;
    }

    return VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_CAL_CONFIG_VCSEL_START, PhasecalVcselStart);
}

esp_err_t VL53L1X_Init(i2c_master_bus_handle_t *p_Bus_Handle, VL53L1X_Dev_t *p_Device,
                       const VL53L1X_Config_t *p_Config)
{
    esp_err_t Error;
    uint8_t ModelID;
    uint8_t BootStatus;
    uint16_t FastOscFrequency;
    uint16_t OuterOffset;
    VL53L1X_DistanceMode_t DistanceMode = VL53L1X_DISTANCE_SHORT;
    VL53L1X_TimingBudget_t TimingBudget = VL53L1X_TIMING_100MS;

    if ((p_Bus_Handle == NULL) || (p_Device == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (p_Config != NULL) {
        DistanceMode = p_Config->DistanceMode;
        TimingBudget = p_Config->TimingBudget;
    }

    memset(p_Device, 0, sizeof(VL53L1X_Dev_t));

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_VL53L1X_I2C_Config, &p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: 0x%X!", Error);

        return Error;
    }

    /* VL53L1X requires at least 1.2 ms boot time after XSHUT is released */
    vTaskDelay(pdMS_TO_TICKS(2));

    /* Verify model ID before soft reset (register 0x010F = 0xEA for VL53L1CXV0FY1) */
    Error = VL53L1X_GetModelID(p_Device, &ModelID);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read model ID: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    if (ModelID != VL53L1X_MODEL_ID_EXPECTED) {
        ESP_LOGE(TAG, "Unexpected model ID: 0x%02X (expected 0x%02X)!", ModelID, VL53L1X_MODEL_ID_EXPECTED);

        i2c_master_bus_rm_device(p_Device->Handle);

        return ESP_ERR_NOT_FOUND;
    }

    /* VL53L1_software_reset() begin: assert reset, hold 1 ms, then release */
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SOFT_RESET, static_cast<uint8_t>(0x00));
    vTaskDelay(pdMS_TO_TICKS(1));
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SOFT_RESET, 0x01);

    /* Give sensor time to boot before first register access */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* VL53L1_poll_for_boot_completion(): wait until FIRMWARE__SYSTEM_STATUS bit 0 = 1 */
    BootStatus = 0x00;
    for (uint32_t Attempts = 0; Attempts < 100; Attempts++) {
        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_FIRMWARE_SYSTEM_STATUS, &BootStatus);
        if (Error != ESP_OK) {
            break;
        }

        if ((BootStatus & 0x01) != 0x00) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to poll boot status: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    if ((BootStatus & 0x01) == 0x00) {
        ESP_LOGE(TAG, "VL53L1X boot timeout. Firmware not ready!");

        i2c_master_bus_rm_device(p_Device->Handle);

        return ESP_ERR_TIMEOUT;
    }

    p_Device->Calibrated = false;

    /* Read oscillator parameters required for timing calculations */
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_OSC_MEASURED_FAST_OSC_FREQUENCY, &FastOscFrequency);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_RESULT_OSC_CALIBRATE_VAL, &p_Device->OscCalibrateVal);

    /* Static config */
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_DSS_CONFIG_TARGET_TOTAL_RATE_MCPS, VL53L1X_TARGET_RATE_MCPS);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_GPIO_TIO_HV_STATUS, 0x02);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SIGMA_EST_PULSE_WIDTH_NS, 0x08);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SIGMA_EST_AMBIENT_WIDTH_NS, 0x10);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_ALGO_CROSSTALK_VALID_HEIGHT_MM, 0x01);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_ALGO_RANGE_IGNORE_VALID_HEIGHT_MM, 0xFF);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_ALGO_RANGE_MIN_CLIP, static_cast<uint8_t>(0x00));
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_ALGO_CONSISTENCY_TOLERANCE, 0x02);

    /* General config */
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_SYSTEM_THRESH_RATE_HIGH, 0x0000);
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_SYSTEM_THRESH_RATE_LOW, 0x0000);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_DSS_CONFIG_APERTURE_ATTENUATION, 0x38);

    /* Timing config */
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_RANGE_CONFIG_SIGMA_THRESH, 360);
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_RANGE_CONFIG_MIN_COUNT_RATE_MCPS, 192);

    /* Dynamic config */
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD_0, 0x01);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD_1, 0x01);
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_CONFIG_QUANTIFIER, 0x02);

    /* Writing GPH0 and GPH1 above sets GPH to 1; restore to 0 before applying timed mode */
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_GROUPED_PARAM_HOLD, static_cast<uint8_t>(0x00));
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_SEED_CONFIG, 0x01);

    /* VL53L1_config_low_power_auto_mode */
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_SEQUENCE_CONFIG, 0x8B);
    VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_DSS_CONFIG_MANUAL_SPADS_SELECT,
                             static_cast<uint16_t>(200 << 8));
    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_DSS_CONFIG_ROI_MODE_CONTROL, 0x02);

    /* Apply distance mode and timing budget from config (or defaults) */
    Error = VL53L1X_SetDistanceMode(p_Device, DistanceMode);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set distance mode: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    Error = VL53L1X_SetTimingBudget(p_Device, TimingBudget);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set timing budget: 0x%X!", Error);

        i2c_master_bus_rm_device(p_Device->Handle);

        return Error;
    }

    /* Pre-program the part-to-part range offset.
     * The API triggers this in VL53L1_init_and_start_range(); assumes MM1 and MM2 are disabled. */
    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_MM_CONFIG_OUTER_OFFSET_MM, &OuterOffset);
    if (Error == ESP_OK) {
        VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_ALGO_PART_TO_PART_RANGE_OFFSET_MM,
                                 static_cast<uint16_t>(OuterOffset * 4));
    }

    ESP_LOGD(TAG, "VL53L1X initialized (model ID: 0x%02X, mode: %s, budget: %d ms)",
             ModelID,
             (DistanceMode == VL53L1X_DISTANCE_SHORT) ? "Short" : "Long",
             static_cast<int>(TimingBudget));
    ESP_LOGD(TAG, "Fast oscillator frequency: %u (4.12 fixed-point)", FastOscFrequency);
    ESP_LOGD(TAG, "Oscillator calibration value: 0x%04X", p_Device->OscCalibrateVal);

    p_Device->IsInitialized = true;

    return ESP_OK;
}

esp_err_t VL53L1X_Deinit(VL53L1X_Dev_t *p_Device)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Device->Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = VL53L1X_StopContinuous(p_Device);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop continuous mode: 0x%X!", Error);

        return Error;
    }

    Error = i2c_master_bus_rm_device(p_Device->Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove I2C device: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "VL53L1X deinitialized");

    p_Device->IsInitialized = false;
    p_Device->Handle = NULL;

    return ESP_OK;
}

esp_err_t VL53L1X_SetDistanceMode(VL53L1X_Dev_t *p_Device, VL53L1X_DistanceMode_t Mode)
{
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (Mode == VL53L1X_DISTANCE_SHORT) {
        /* Short mode: VCSEL period A = 7, B = 5; phase guard ~0x38 */
        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_RANGE_VCSEL_PERIOD_A, 0x07);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_RANGE_VCSEL_PERIOD_B, 0x05);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_VALID_PHASE_HIGH, 0x38);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_WOI_SD0, 0x07);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_WOI_SD1, 0x05);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_INITIAL_PHASE_SD0, 0x06);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_INITIAL_PHASE_SD1, 0x06);
        if (Error != ESP_OK) {
            return Error;
        }
    } else {
        /* Long mode: VCSEL period A = 15, B = 10; phase guard ~0xB8 */
        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_RANGE_VCSEL_PERIOD_A, 0x0F);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_RANGE_VCSEL_PERIOD_B, 0x0D);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_VALID_PHASE_HIGH, 0xB8);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_WOI_SD0, 0x0F);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_WOI_SD1, 0x0D);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_INITIAL_PHASE_SD0, 0x0E);
        if (Error != ESP_OK) {
            return Error;
        }

        Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SD_INITIAL_PHASE_SD1, 0x0E);
        if (Error != ESP_OK) {
            return Error;
        }
    }

    p_Device->DistanceMode = Mode;

    ESP_LOGD(TAG, "Distance mode set to %s", (Mode == VL53L1X_DISTANCE_SHORT) ? "Short" : "Long");

    return ESP_OK;
}

esp_err_t VL53L1X_SetTimingBudget(VL53L1X_Dev_t *p_Device, VL53L1X_TimingBudget_t Budget)
{
    int Index;
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    Index = VL53L1X_TimingBudget_ToIndex(Budget);
    if (Index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((p_Device->DistanceMode == VL53L1X_DISTANCE_LONG) && (Budget == VL53L1X_TIMING_15MS)) {
        ESP_LOGE(TAG, "15 ms timing budget is not supported in Long distance mode!");

        return ESP_ERR_INVALID_ARG;
    }

    const VL53L1X_TimingConfig_t *p_Table = (p_Device->DistanceMode == VL53L1X_DISTANCE_SHORT) ?
                                            _VL53L1X_Timing_Short : _VL53L1X_Timing_Long;

    Error = VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_RANGE_TIMEOUT_A_HI, p_Table[Index].MacropA);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write TIMEOUT_MACROP_A: 0x%X!", Error);

        return Error;
    }

    Error = VL53L1X_Write_Register16(&p_Device->Handle, VL53L1X_REG_RANGE_TIMEOUT_B_HI, p_Table[Index].MacropB);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write TIMEOUT_MACROP_B: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "Timing budget set to %d ms (A=0x%04X, B=0x%04X)",
             static_cast<int>(Budget), p_Table[Index].MacropA, p_Table[Index].MacropB);

    return ESP_OK;
}

esp_err_t VL53L1X_StartContinuous(VL53L1X_Dev_t *p_Device, uint32_t PeriodMs)
{
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = VL53L1X_Write_Register32(&p_Device->Handle, VL53L1X_REG_SYSTEM_INTERMEASUREMENT_PERIOD,
                                     static_cast<uint32_t>(PeriodMs * p_Device->OscCalibrateVal));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write inter-measurement period: 0x%X!", Error);

        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR, static_cast<uint8_t>(0x01));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear interrupt: 0x%X!", Error);

        return Error;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_MODE_START, VL53L1X_MODE_CONTINUOUS);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start continuous ranging: 0x%X!", Error);

        return Error;
    }

    ESP_LOGD(TAG, "Continuous ranging started (period: %u ms)", static_cast<unsigned>(PeriodMs));

    return ESP_OK;
}

esp_err_t VL53L1X_StopContinuous(VL53L1X_Dev_t *p_Device)
{
    esp_err_t Error;

    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_MODE_START, static_cast<uint8_t>(0x80));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to abort ranging: 0x%X!", Error);

        return Error;
    }

    p_Device->Calibrated = false;

    if (p_Device->SavedVhvInit != 0x00) {
        VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_INIT_RANGING, p_Device->SavedVhvInit);
    }

    if (p_Device->SavedVhvTimeout != 0x00) {
        VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_VHV_CONFIG_TIMEOUT, p_Device->SavedVhvTimeout);
    }

    VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_PHASECAL_CONFIG_OVERRIDE, static_cast<uint8_t>(0x00));

    ESP_LOGD(TAG, "Continuous ranging stopped");

    return ESP_OK;
}

esp_err_t VL53L1X_IsDataReady(VL53L1X_Dev_t *p_Device, bool *p_Ready)
{
    uint8_t MuxCtrl;
    uint8_t Status;
    uint8_t IntPol;
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Ready == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_GPIO_HV_MUX_CTRL, &MuxCtrl);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GPIO_HV_MUX_CTRL: 0x%X!", Error);

        return Error;
    }

    IntPol = ((MuxCtrl & 0x10) == 0) ? 1U : 0U;

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_GPIO_TIO_HV_STATUS, &Status);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GPIO_TIO_HV_STATUS: 0x%X!", Error);

        return Error;
    }

    *p_Ready = ((Status & VL53L1X_DATA_READY_BIT) == IntPol);

    return ESP_OK;
}

esp_err_t VL53L1X_GetResult(VL53L1X_Dev_t *p_Device, VL53L1X_Result_t *p_Result)
{
    uint8_t Buffer[17];
    uint8_t AddrBuffer[2];
    esp_err_t Error;

    if ((p_Device == NULL) || (p_Result == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    AddrBuffer[0] = static_cast<uint8_t>((VL53L1X_REG_RESULT_RANGE_STATUS >> 8) & 0xFF);
    AddrBuffer[1] = static_cast<uint8_t>(VL53L1X_REG_RESULT_RANGE_STATUS & 0xFF);

    Error = I2CM_Write(&p_Device->Handle, AddrBuffer, sizeof(AddrBuffer));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set result register address: 0x%X!", Error);

        return Error;
    }

    Error = I2CM_Read(&p_Device->Handle, Buffer, sizeof(Buffer));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read result registers: 0x%X!", Error);

        return Error;
    }

    if (p_Device->Calibrated == false) {
        Error = VL53L1X_Setup_Manual_Calibration(p_Device);
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Manual calibration setup failed: 0x%X", Error);
        }

        p_Device->Calibrated = true;
    }

    /* Buffer layout from 0x0089:
     * [0]      = RANGE_STATUS (bits [4:0] are range_status)
     * [2..3]   = SPAD_NB effective (16-bit big-endian)
     * [5..6]   = SIGNAL_RATE (16-bit big-endian)
     * [7..8]   = AMBIENT_RATE (16-bit big-endian)
     * [13..14] = DISTANCE_MM (16-bit big-endian) at offset 0x0096 - 0x0089 = 13 */
    p_Result->Status = VL53L1X_Map_Range_Status(Buffer[0] & 0x1F);
    p_Result->EffectiveSPADs = (static_cast<uint16_t>(Buffer[3]) << 8) | static_cast<uint16_t>(Buffer[4]);
    p_Result->SignalRateMCPS = (static_cast<uint16_t>(Buffer[5]) << 8) | static_cast<uint16_t>(Buffer[6]);
    p_Result->AmbientRateMCPS = (static_cast<uint16_t>(Buffer[7]) << 8) | static_cast<uint16_t>(Buffer[8]);
    p_Result->Distance_mm = (static_cast<uint16_t>(Buffer[13]) << 8) | static_cast<uint16_t>(Buffer[14]);

    ESP_LOGD(TAG, "Range: %d mm | Status: %d | Signal: %u | Ambient: %u | SPADs: %u",
             p_Result->Distance_mm, static_cast<int>(p_Result->Status),
             p_Result->SignalRateMCPS, p_Result->AmbientRateMCPS, p_Result->EffectiveSPADs);

    return ESP_OK;
}

esp_err_t VL53L1X_ClearInterrupt(VL53L1X_Dev_t *p_Device)
{
    if (p_Device == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_Device->IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
}

esp_err_t VL53L1X_GetModelID(VL53L1X_Dev_t *p_Device, uint8_t *p_ModelID)
{
    esp_err_t Error;

    if ((p_Device == NULL) || (p_ModelID == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = VL53L1X_Write_Register8(&p_Device->Handle, VL53L1X_REG_MODEL_ID, p_ModelID);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MODEL_ID register: 0x%X!", Error);

        return Error;
    }

    return ESP_OK;
}
