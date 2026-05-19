/*
 * i2c.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: I2C master driver interface.
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

#ifndef I2C_H_
#define I2C_H_

#include <stdint.h>
#include <stdbool.h>

#include <driver/i2c_master.h>

#include <esp_err.h>

/** @brief              Initialize the I2C master bus.
 *                      Creates an I2C master bus with specified configuration (GPIO pins,
 *                      clock speed, pullups, etc.). Bus can be shared by multiple devices.
 *  @note               Configure GPIO pullups in p_Config if not hardware-pulled.
 *                      Typical clock speeds: 100kHz (standard), 400kHz (fast).
 *  @param p_Config     Pointer to i2c_master_bus_config_t configuration
 *  @param p_Bus_Handle Pointer to store the created bus handle
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL or config invalid
 *                      ESP_ERR_NO_MEM if bus handle allocation fails
 *                      ESP_FAIL if bus initialization fails
 */
int32_t I2CM_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *p_Bus_Handle);

/** @brief              Deinitialize the I2C master bus.
 *                      Removes the I2C bus and frees all associated resources. All device
 *                      handles on this bus must be removed first.
 *  @note               Remove all devices with i2c_master_bus_rm_device() first.
 *  @warning            Bus handle becomes invalid after this call.
 *  @param Bus_Handle   I2C bus handle to deinitialize
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_STATE if devices still attached
 *                      ESP_FAIL if bus removal fails
 */
int32_t I2CM_Deinit(i2c_master_bus_handle_t Bus_Handle);

/** @brief              Transmit data over the I2C interface.
 *                      Sends data to an I2C device. Uses blocking transmission with timeout.
 *  @note               Default timeout: 1000ms.
 *                      Device must be added to bus before calling this.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param p_Data       Pointer to data buffer to transmit
 *  @param Length       Number of bytes to transmit
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Dev_Handle or p_Data is NULL
 *                      ESP_ERR_INVALID_STATE if I2C bus not initialized
 *                      ESP_ERR_TIMEOUT if transaction times out
 *                      ESP_FAIL if transmission fails (NACK, bus error)
 */
int32_t I2CM_Write(i2c_master_dev_handle_t *p_Dev_Handle, const uint8_t *p_Data, uint32_t Length);

/** @brief              Receive data from the I2C interface.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param p_Data       Pointer to data
 *  @param Length       Length of data in bytes
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
int32_t I2CM_Read(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t *p_Data, uint32_t Length);

/** @brief              Write then read from I2C device in a single atomic transaction.
 *                      Sends the write buffer, then issues a repeated-START and reads into
 *                      the read buffer using i2c_master_transmit_receive. Use this instead
 *                      of separate I2CM_Write + I2CM_Read calls to avoid
 *                      ESP_ERR_INVALID_STATE (0x103) with the ESP-IDF v5 I2C master driver.
 *  @note               Default timeout: 1000ms.
 *                      Device must be added to bus before calling this.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param p_WriteData  Pointer to data buffer to transmit (e.g. register address byte(s))
 *  @param WriteLength  Number of bytes to transmit
 *  @param p_ReadData   Pointer to buffer for received data
 *  @param ReadLength   Number of bytes to receive
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if any pointer is NULL or a length is zero
 *                      ESP_ERR_TIMEOUT if mutex or I2C transaction times out
 *                      ESP_FAIL if the transaction fails (NACK, bus error)
 */
int32_t I2CM_WriteRead(i2c_master_dev_handle_t *p_Dev_Handle,
                       const uint8_t *p_WriteData, uint32_t WriteLength,
                       uint8_t *p_ReadData, uint32_t ReadLength);

/** @brief              Modify the content of a register.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param Register     Register address
 *  @param Mask         Bit mask
 *  @param Value        Bit level
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
int32_t I2CM_ModifyRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t Mask, uint8_t Value);

/** @brief              Scan the I2C bus for connected devices.
 *                      Probes all valid 7-bit addresses (0x08–0x77) and logs each responding
 *                      device address via ESP_LOGI. Addresses 0x00–0x07 and 0x78–0x7F are
 *                      reserved by the I2C specification and are skipped.
 *  @note               This function is intended for debugging only and should not be called
 *                      in production code. The scan temporarily occupies the I2C bus mutex
 *                      for each probe, so other I2C operations are blocked during the scan.
 *  @warning            Do not call while time-critical I2C transfers are running.
 *  @param Bus_Handle   I2C master bus handle to scan on
 *  @return             ESP_OK on success (regardless of how many devices were found)
 *                      ESP_ERR_INVALID_ARG if Bus_Handle is NULL
 */
int32_t I2CM_Scan(i2c_master_bus_handle_t Bus_Handle);

#endif /* I2C_H_ */