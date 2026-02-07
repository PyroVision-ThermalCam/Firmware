/*
 * rv8263c8.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: RV8263-C8 Real-Time Clock driver definition.
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

#ifndef RV8263C8_H_
#define RV8263C8_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "../I2C/i2c.h"

/** @brief Alarm configuration structure.
 */
typedef struct {
    uint8_t Seconds;        /**< Alarm seconds (0-59) */
    uint8_t Minutes;        /**< Alarm minutes (0-59) */
    uint8_t Hours;          /**< Alarm hours (0-23) */
    uint8_t Day;            /**< Alarm day of month (1-31) */
    uint8_t Weekday;        /**< Alarm weekday (0-6) */
    bool EnableSeconds;     /**< Enable seconds matching */
    bool EnableMinutes;     /**< Enable minutes matching */
    bool EnableHours;       /**< Enable hours matching */
    bool EnableDay;         /**< Enable day matching */
    bool EnableWeekday;     /**< Enable weekday matching */
} RTC_Alarm_t;

/** @brief Timer frequency options.
 */
typedef enum {
    RTC_TIMER_FREQ_4096HZ = 0,  /**< 4096 Hz (244 µs resolution) */
    RTC_TIMER_FREQ_64HZ   = 1,  /**< 64 Hz (15.625 ms resolution) */
    RTC_TIMER_FREQ_1HZ    = 2,  /**< 1 Hz (1 second resolution) */
    RTC_TIMER_FREQ_1_60HZ = 3,  /**< 1/60 Hz (1 minute resolution) */
} RTC_TimerFreq_t;

/** @brief              Initialize the RV8263-C8 RTC.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_Init(i2c_master_bus_handle_t *p_Bus_Handle, i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Deinitialize the RTC driver.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_Deinit(i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Get the current time from the RTC.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param p_Time       Pointer to store the time
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_GetTime(i2c_master_dev_handle_t *p_Dev_Handle, struct tm *p_Time);

/** @brief              Set the time on the RTC.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param p_Time       Pointer to the time to set
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_SetTime(i2c_master_dev_handle_t *p_Dev_Handle, const struct tm *p_Time);

/** @brief              Configure the alarm.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param p_Alarm      Pointer to alarm configuration
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_SetAlarm(i2c_master_dev_handle_t *p_Dev_Handle, const RTC_Alarm_t *p_Alarm);

/** @brief              Enable or disable the alarm interrupt.
 *  @param p_Dev_Handle Pointer to device handle
 *  @param Enable       true to enable, false to disable
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_EnableAlarmInterrupt(i2c_master_dev_handle_t *p_Dev_Handle, bool Enable);

/** @brief              Clear the alarm flag.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_ClearAlarmFlag(i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Check if the alarm has been triggered.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             true if alarm is triggered, false otherwise
 */
bool RTC_IsAlarmTriggered(i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief                  Configure and start the countdown timer.
 *  @param p_Dev_Handle     Pointer to device handle
 *  @param Value            Timer countdown value (0-255)
 *  @param Frequency        Timer clock frequency
 *  @param InterruptEnable  Enable timer interrupt
 *  @return                 ESP_OK when successful
 */
esp_err_t RTC_SetTimer(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Value, RTC_TimerFreq_t Frequency,
                       bool InterruptEnable);

/** @brief              Stop the countdown timer.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_StopTimer(i2c_master_dev_handle_t *p_Dev_Handle);

/** @brief              Perform a software reset of the RTC.
 *  @param p_Dev_Handle Pointer to device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RTC_SoftwareReset(i2c_master_dev_handle_t *p_Dev_Handle);

#ifdef DEBUG
/** @brief              Dump all RTC registers for debugging.
 *  @param p_Dev_Handle Pointer to device handle
 */
void RTC_DumpRegisters(i2c_master_dev_handle_t *p_Dev_Handle);
#endif

#endif /* RV8263C8_H_ */