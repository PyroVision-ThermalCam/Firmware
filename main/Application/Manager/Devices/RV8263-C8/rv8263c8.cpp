/*
 * rv8263c8.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: RV8263-C8 Real-Time Clock driver implementation.
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

#include "rv8263c8.h"

#include <sdkconfig.h>

/* RV8263-C8 I2C Address */
#define ADDR_RV8263C8                       0x51

/* RV8263-C8 Register Addresses */
#define RV8263_REG_CONTROL1                 0x00
#define RV8263_REG_CONTROL2                 0x01
#define RV8263_REG_OFFSET                   0x02
#define RV8263_REG_RAM                      0x03
#define RV8263_REG_SECONDS                  0x04
#define RV8263_REG_MINUTES                  0x05
#define RV8263_REG_HOURS                    0x06
#define RV8263_REG_DATE                     0x07
#define RV8263_REG_WEEKDAY                  0x08
#define RV8263_REG_MONTH                    0x09
#define RV8263_REG_YEAR                     0x0A
#define RV8263_REG_SECONDS_ALARM            0x0B
#define RV8263_REG_MINUTES_ALARM            0x0C
#define RV8263_REG_HOURS_ALARM              0x0D
#define RV8263_REG_DATE_ALARM               0x0E
#define RV8263_REG_WEEKDAY_ALARM            0x0F
#define RV8263_REG_TIMER_VALUE              0x10
#define RV8263_REG_TIMER_MODE               0x11

/* Control1 Register Bits */
#define RV8263_CTRL1_CAP_SEL                (1 << 0)    /* Oscillator capacitance selection */
#define RV8263_CTRL1_STOP                   (1 << 5)    /* Stop RTC clock */
#define RV8263_CTRL1_SR                     (1 << 7)    /* Software reset */

/* Control2 Register Bits */
#define RV8263_CTRL2_CLKIE                  (1 << 0)    /* Clock output interrupt enable */
#define RV8263_CTRL2_TIE                    (1 << 1)    /* Timer interrupt enable */
#define RV8263_CTRL2_AIE                    (1 << 2)    /* Alarm interrupt enable */
#define RV8263_CTRL2_TF                     (1 << 3)    /* Timer flag */
#define RV8263_CTRL2_AF                     (1 << 4)    /* Alarm flag */
#define RV8263_CTRL2_CTAIE                  (1 << 5)    /* Countdown timer A interrupt enable */
#define RV8263_CTRL2_CTBIE                  (1 << 6)    /* Countdown timer B interrupt enable */
#define RV8263_CTRL2_CTAF                   (1 << 7)    /* Countdown timer A flag */

/* Timer Mode Register Bits */
#define RV8263_TIMER_TE                     (1 << 2)    /* Timer enable */
#define RV8263_TIMER_TIE                    (1 << 3)    /* Timer interrupt enable */
#define RV8263_TIMER_TI_TP                  (1 << 4)    /* Timer interrupt mode (pulse/level) */
#define RV8263_TIMER_TD_MASK                0x03        /* Timer clock frequency mask */
#define RV8263_TIMER_TD_4096HZ              0x00        /* 4096 Hz */
#define RV8263_TIMER_TD_64HZ                0x01        /* 64 Hz */
#define RV8263_TIMER_TD_1HZ                 0x02        /* 1 Hz */
#define RV8263_TIMER_TD_1_60HZ              0x03        /* 1/60 Hz */

/* Alarm Register Bits */
#define RV8263_ALARM_AE                     (1 << 7)    /* Alarm enable (0 = enabled) */

/* Seconds Register */
#define RV8263_SECONDS_OS                   (1 << 7)    /* Oscillator stop flag */

/* Month Register */
#define RV8263_MONTH_CENTURY                (1 << 7)    /* Century bit */

#define MAX_RTC_REGS 64

static i2c_device_config_t _RTC_I2C_Config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADDR_RV8263C8,
    .scl_speed_hz = 400000,
    .scl_wait_us = 0,
    .flags = {
        .disable_ack_check = 0,
    },
};

static const char *TAG                      = "RTC";

/** @brief      Convert BCD to binary.
 *  @param BCD  BCD value
 *  @return     Binary value
 */
static uint8_t RTC_BCD2Bin(uint8_t BCD)
{
    return ((BCD >> 4) * 10) + (BCD & 0x0F);
}

/** @brief      Convert binary to BCD.
 *  @param Bin  Binary value
 *  @return     BCD value
 */
static uint8_t RTC_Bin2BCD(uint8_t Bin)
{
    return ((Bin / 10) << 4) | (Bin % 10);
}

/** @brief              Read a single register from the RTC.
 *  @param Register     Register address
 *  @param p_Data       Pointer to store the read value
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
static esp_err_t RTC_ReadRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t *p_Data)
{
    esp_err_t Error;

    Error = I2CM_Write(p_Dev_Handle, &Register, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    return I2CM_Read(p_Dev_Handle, p_Data, 1);
}

/** @brief              Write a single register to the RTC.
 *  @param Register     Register address
 *  @param Data         Data to write
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
static esp_err_t RTC_WriteRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t Data)
{
    uint8_t Buffer[2] = {Register, Data};

    return I2CM_Write(p_Dev_Handle, Buffer, sizeof(Buffer));
}

/** @brief              Read multiple consecutive registers from the RTC.
 *  @param Register     Starting register address
 *  @param p_Data       Pointer to store the read values
 *  @param Length       Number of bytes to read
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
static esp_err_t RTC_ReadRegisters(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t *p_Data, uint8_t Length)
{
    esp_err_t Error;

    Error = I2CM_Write(p_Dev_Handle, &Register, 1);
    if (Error != ESP_OK) {
        return Error;
    }

    return I2CM_Read(p_Dev_Handle, p_Data, Length);
}

/** @brief              Write multiple consecutive registers to the RTC.
 *  @param Register     Starting register address
 *  @param p_Data       Pointer to data to write
 *  @param Length       Number of bytes to write
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
static esp_err_t RTC_WriteRegisters(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, const uint8_t *p_Data, uint8_t Length)
{
    uint8_t Buffer[MAX_RTC_REGS + 1];

    if (Length > MAX_RTC_REGS) {
        return ESP_ERR_INVALID_SIZE;
    }

    Buffer[0] = Register;
    memcpy(&Buffer[1], p_Data, Length);

    return I2CM_Write(p_Dev_Handle, Buffer, Length + 1);
}

esp_err_t RTC_Init(i2c_master_bus_handle_t *p_Bus_Handle, i2c_master_dev_handle_t *p_Dev_Handle)
{
    esp_err_t Error;
    uint8_t Control1;
    uint8_t Seconds;

    Error = i2c_master_bus_add_device(*p_Bus_Handle, &_RTC_I2C_Config, p_Dev_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %d", Error);
        return Error;
    }

    ESP_LOGD(TAG, "Initialize RV8263-C8 RTC...");

    /* Check oscillator stop flag */
    Error = RTC_ReadRegister(p_Dev_Handle, RV8263_REG_SECONDS, &Seconds);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read seconds register: %d", Error);

        return Error;
    }

    if (Seconds & RV8263_SECONDS_OS) {
        ESP_LOGW(TAG, "Oscillator was stopped - time may be invalid!");

        /* Clear OS flag by writing seconds register */
        Error = RTC_WriteRegister(p_Dev_Handle, RV8263_REG_SECONDS, Seconds & ~RV8263_SECONDS_OS);
        if (Error != ESP_OK) {
            return Error;
        }
    }

    /* Read Control1 register */
    Error = RTC_ReadRegister(p_Dev_Handle, RV8263_REG_CONTROL1, &Control1);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read control register: %d", Error);

        return Error;
    }

    /* Ensure oscillator is running */
    if (Control1 & RV8263_CTRL1_STOP) {
        ESP_LOGD(TAG, "Starting RTC oscillator...");

        Control1 &= ~RV8263_CTRL1_STOP;
        Error = RTC_WriteRegister(p_Dev_Handle, RV8263_REG_CONTROL1, Control1);
        if (Error != ESP_OK) {
            return Error;
        }
    }

    ESP_LOGD(TAG, "RV8263-C8 RTC initialized successfully");

    return ESP_OK;
}

esp_err_t RTC_Deinit(i2c_master_dev_handle_t *p_Dev_Handle)
{
    if (p_Dev_Handle != NULL) {
        esp_err_t Error;

        Error = i2c_master_bus_rm_device(*p_Dev_Handle);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remove I2C device: %d!", Error);

            return Error;
        }

        p_Dev_Handle = NULL;
    }

    return ESP_OK;
}

esp_err_t RTC_GetTime(i2c_master_dev_handle_t *p_Dev_Handle, struct tm *p_Time)
{
    esp_err_t Error;
    uint8_t Buffer[7];

    if ((p_Time == NULL) || (p_Dev_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read time registers (Seconds to Year) */
    Error = RTC_ReadRegisters(p_Dev_Handle, RV8263_REG_SECONDS, Buffer, sizeof(Buffer));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time: %d!", Error);

        return Error;
    }

    /* Convert BCD to binary and map to struct tm format */
    p_Time->tm_sec = RTC_BCD2Bin(Buffer[0] & 0x7F);         /* Seconds (0-59) */
    p_Time->tm_min = RTC_BCD2Bin(Buffer[1] & 0x7F);         /* Minutes (0-59) */
    p_Time->tm_hour = RTC_BCD2Bin(Buffer[2] & 0x3F);        /* Hours (0-23) */
    p_Time->tm_mday = RTC_BCD2Bin(Buffer[3] & 0x3F);        /* Day of month (1-31) */
    p_Time->tm_wday = Buffer[4] & 0x07;                     /* Day of week (0-6) */
    p_Time->tm_mon = RTC_BCD2Bin(Buffer[5] & 0x1F) - 1;     /* Month (0-11, struct tm uses 0-based) */
    p_Time->tm_year = RTC_BCD2Bin(Buffer[6]) + 100;         /* Years since 1900 (2000 = 100) */

    /* Check century bit */
    if (Buffer[5] & RV8263_MONTH_CENTURY) {
        p_Time->tm_year += 100;
    }

    /* Calculate day of year */
    p_Time->tm_yday = 0;

    /* Unknown DST status */
    p_Time->tm_isdst = -1;

    return ESP_OK;
}

esp_err_t RTC_SetTime(i2c_master_dev_handle_t *p_Dev_Handle, const struct tm *p_Time)
{
    uint8_t Buffer[7];
    int Year;
    int Month;

    if ((p_Time == NULL) || (p_Dev_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Convert struct tm format to RTC values */
    Year = p_Time->tm_year + 1900;      /* tm_year is years since 1900 */
    Month = p_Time->tm_mon + 1;         /* tm_mon is 0-11, RTC needs 1-12 */

    /* Validate input */
    if ((p_Time->tm_sec > 59) || (p_Time->tm_min > 59) || (p_Time->tm_hour > 23) ||
        (p_Time->tm_mday < 1) || (p_Time->tm_mday > 31) || (Month < 1) || (Month > 12) ||
        (p_Time->tm_wday > 6)) {
        ESP_LOGE(TAG, "Invalid time values!");

        return ESP_ERR_INVALID_ARG;
    }

    /* Convert binary to BCD */
    Buffer[0] = RTC_Bin2BCD(p_Time->tm_sec);
    Buffer[1] = RTC_Bin2BCD(p_Time->tm_min);
    Buffer[2] = RTC_Bin2BCD(p_Time->tm_hour);
    Buffer[3] = RTC_Bin2BCD(p_Time->tm_mday);
    Buffer[4] = p_Time->tm_wday;
    Buffer[5] = RTC_Bin2BCD(Month);
    Buffer[6] = RTC_Bin2BCD(Year % 100);

    /* Set century bit if year >= 2100 */
    if (Year >= 2100) {
        Buffer[5] |= RV8263_MONTH_CENTURY;
    }

    ESP_LOGD(TAG, "Setting time: %04d-%02d-%02d %02d:%02d:%02d",
             Year, Month, p_Time->tm_mday,
             p_Time->tm_hour, p_Time->tm_min, p_Time->tm_sec);

    return RTC_WriteRegisters(p_Dev_Handle, RV8263_REG_SECONDS, Buffer, sizeof(Buffer));
}

esp_err_t RTC_SetAlarm(i2c_master_dev_handle_t *p_Dev_Handle, const RTC_Alarm_t *p_Alarm)
{
    uint8_t Buffer[5];

    if ((p_Alarm == NULL) || (p_Dev_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Configure alarm registers - set AE bit to 1 to disable, 0 to enable */
    Buffer[0] = p_Alarm->EnableSeconds ? RTC_Bin2BCD(p_Alarm->Seconds) : RV8263_ALARM_AE;
    Buffer[1] = p_Alarm->EnableMinutes ? RTC_Bin2BCD(p_Alarm->Minutes) : RV8263_ALARM_AE;
    Buffer[2] = p_Alarm->EnableHours ? RTC_Bin2BCD(p_Alarm->Hours) : RV8263_ALARM_AE;
    Buffer[3] = p_Alarm->EnableDay ? RTC_Bin2BCD(p_Alarm->Day) : RV8263_ALARM_AE;
    Buffer[4] = p_Alarm->EnableWeekday ? p_Alarm->Weekday : RV8263_ALARM_AE;

    return RTC_WriteRegisters(p_Dev_Handle, RV8263_REG_SECONDS_ALARM, Buffer, sizeof(Buffer));
}

esp_err_t RTC_EnableAlarmInterrupt(i2c_master_dev_handle_t *p_Dev_Handle, bool Enable)
{
    return I2CM_ModifyRegister(p_Dev_Handle, RV8263_REG_CONTROL2, RV8263_CTRL2_AIE,
                               Enable ? RV8263_CTRL2_AIE : 0);
}

esp_err_t RTC_ClearAlarmFlag(i2c_master_dev_handle_t *p_Dev_Handle)
{
    return I2CM_ModifyRegister(p_Dev_Handle, RV8263_REG_CONTROL2, RV8263_CTRL2_AF, 0);
}

bool RTC_IsAlarmTriggered(i2c_master_dev_handle_t *p_Dev_Handle)
{
    uint8_t Control2;

    if (RTC_ReadRegister(p_Dev_Handle, RV8263_REG_CONTROL2, &Control2) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read Control2 register for alarm status!");

        return false;
    }

    return (Control2 & RV8263_CTRL2_AF) != 0;
}

esp_err_t RTC_SetTimer(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Value, RTC_TimerFreq_t Frequency, bool InterruptEnable)
{
    esp_err_t Error;
    uint8_t TimerMode;

    if (p_Dev_Handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Set timer value */
    Error = RTC_WriteRegister(p_Dev_Handle, RV8263_REG_TIMER_VALUE, Value);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Configure timer mode */
    TimerMode = RV8263_TIMER_TE | (static_cast<uint8_t>(Frequency) & RV8263_TIMER_TD_MASK);
    if (InterruptEnable) {
        TimerMode |= RV8263_TIMER_TIE;
    }

    return RTC_WriteRegister(p_Dev_Handle, RV8263_REG_TIMER_MODE, TimerMode);
}

esp_err_t RTC_StopTimer(i2c_master_dev_handle_t *p_Dev_Handle)
{
    return RTC_WriteRegister(p_Dev_Handle, RV8263_REG_TIMER_MODE, 0);
}

esp_err_t RTC_SoftwareReset(i2c_master_dev_handle_t *p_Dev_Handle)
{
    ESP_LOGD(TAG, "Performing software reset...");

    return RTC_WriteRegister(p_Dev_Handle, RV8263_REG_CONTROL1, RV8263_CTRL1_SR);
}

#ifdef DEBUG
void RTC_DumpRegisters(i2c_master_dev_handle_t *p_Dev_Handle)
{
    uint8_t Data;

    ESP_LOGI(TAG, "Register dump:");

    for (uint8_t i = 0x00; i <= 0x11; i++) {
        if (RTC_ReadRegister(p_Dev_Handle, i, &Data) == ESP_OK) {
            ESP_LOGI(TAG, "    Register 0x%02X: 0x%02X", i, Data);
        }
    }
}
#endif