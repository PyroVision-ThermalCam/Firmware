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

/** @brief RTC device instance.
 */
typedef struct {
    i2c_master_dev_handle_t Handle;     /**< I2C device handle. */
} RV8263C8_Dev_t;

/** @brief Alarm configuration structure.
 */
typedef struct {
    uint8_t Seconds;            /**< Alarm seconds (0-59) */
    uint8_t Minutes;            /**< Alarm minutes (0-59) */
    uint8_t Hours;              /**< Alarm hours (0-23) */
    uint8_t Day;                /**< Alarm day of month (1-31) */
    uint8_t Weekday;            /**< Alarm weekday (0-6) */
    bool EnableSeconds;         /**< Enable seconds matching */
    bool EnableMinutes;         /**< Enable minutes matching */
    bool EnableHours;           /**< Enable hours matching */
    bool EnableDay;             /**< Enable day matching */
    bool EnableWeekday;         /**< Enable weekday matching */
} RV8263C8_Alarm_t;

/** @brief Timer frequency options.
 */
typedef enum {
    RTC_TIMER_FREQ_4096HZ = 0,  /**< 4096 Hz (244 µs resolution) */
    RTC_TIMER_FREQ_64HZ = 1,    /**< 64 Hz (15.625 ms resolution) */
    RTC_TIMER_FREQ_1HZ = 2,     /**< 1 Hz (1 second resolution) */
    RTC_TIMER_FREQ_1_60HZ = 3,  /**< 1/60 Hz (1 minute resolution) */
} RV8263C8_TimerFreq_t;

/** @brief              Initialize the RV8263-C8 RTC.
 *  @param p_Bus_Handle Pointer to I2C bus handle
 *  @param p_Dev_Handle Pointer to store the created device handle
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_Init(i2c_master_bus_handle_t *p_Bus_Handle, RV8263C8_Dev_t *p_Device);

/** @brief              Deinitialize the RTC driver.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_Deinit(RV8263C8_Dev_t *p_Device);

/** @brief              Get the current time from the RTC.
 *  @param p_Device     Pointer to device instance
 *  @param p_Time       Pointer to store the time
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_GetTime(RV8263C8_Dev_t *p_Device, struct tm *p_Time);

/** @brief              Set the time on the RTC.
 *  @param p_Device     Pointer to device instance
 *  @param p_Time       Pointer to the time to set
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_SetTime(RV8263C8_Dev_t *p_Device, const struct tm *p_Time);

/** @brief              Configure the alarm.
 *  @param p_Device     Pointer to device instance
 *  @param p_Alarm      Pointer to alarm configuration
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_SetAlarm(RV8263C8_Dev_t *p_Device, const RV8263C8_Alarm_t *p_Alarm);

/** @brief              Enable or disable the alarm interrupt.
 *  @param p_Device     Pointer to device instance
 *  @param Enable       true to enable, false to disable
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_EnableAlarmInterrupt(RV8263C8_Dev_t *p_Device, bool Enable);

/** @brief              Clear the alarm flag.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_ClearAlarmFlag(RV8263C8_Dev_t *p_Device);

/** @brief              Check if the alarm has been triggered.
 *  @param p_Device     Pointer to device instance
 *  @return             true if alarm is triggered, false otherwise
 */
bool RV8263C8_IsAlarmTriggered(RV8263C8_Dev_t *p_Device);

/** @brief                  Configure and start the countdown timer.
 *  @param p_Device         Pointer to device instance
 *  @param Value            Timer countdown value (0-255)
 *  @param Frequency        Timer clock frequency
 *  @param InterruptEnable  Enable timer interrupt
 *  @return                 ESP_OK when successful
 */
esp_err_t RV8263C8_SetTimer(RV8263C8_Dev_t *p_Device, uint8_t Value, RV8263C8_TimerFreq_t Frequency,
                            bool InterruptEnable);

/** @brief              Stop the countdown timer.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_StopTimer(RV8263C8_Dev_t *p_Device);

/** @brief              Perform a software reset of the RTC.
 *  @param p_Device     Pointer to device instance
 *  @return             ESP_OK when successful
 */
esp_err_t RV8263C8_SoftwareReset(RV8263C8_Dev_t *p_Device);

#ifdef DEBUG
/** @brief              Dump all RTC registers for debugging.
 *  @param p_Device     Pointer to device instance
 */
void RV8263C8_DumpRegisters(RV8263C8_Dev_t *p_Device);
#endif

#endif /* RV8263C8_H_ */