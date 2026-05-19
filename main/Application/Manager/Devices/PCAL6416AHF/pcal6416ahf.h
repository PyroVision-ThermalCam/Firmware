/*
 * pcal6416ahf.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: PCAL6416AHF Port Expander driver definition.
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

#ifndef PCAL6416AHF_H_
#define PCAL6416AHF_H_

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#include "../I2C/i2c.h"

/** @brief Port number for port expander pins.
 */
typedef enum {
    PCAL6416_PORT_0 = 0x00,     /**< Port 0 (P0.0 – P0.7). */
    PCAL6416_PORT_1 = 0x01,     /**< Port 1 (P1.0 – P1.7). */
} PCAL6416_Port_t;

/** @brief I/O direction for a port expander pin.
 */
typedef enum {
    PCAL6416_DIR_INPUT  = 0x00, /**< Pin configured as input. */
    PCAL6416_DIR_OUTPUT = 0x01, /**< Pin configured as output. */
} PCAL6416_Dir_t;

/** @brief Pull-resistor selection for a port expander pin.
 */
typedef enum {
    PCAL6416_PULL_NONE = 0x00,  /**< No pull resistor. */
    PCAL6416_PULL_UP   = 0x01,  /**< Pull-up resistor enabled. */
    PCAL6416_PULL_DOWN = 0x02,  /**< Pull-down resistor enabled. */
} PCAL6416_Pull_t;

/** @brief Configuration for a single I/O pin of the PCAL6416AHF.
 */
typedef struct {
    PCAL6416_Port_t Port;       /**< Port number (PCAL6416_PORT_0 or PCAL6416_PORT_1). */
    uint8_t Pin;                /**< Pin number within the port (0-7). */
    PCAL6416_Dir_t Direction;   /**< Input or output. */
    PCAL6416_Pull_t Pull;       /**< Pull-up, pull-down, or none. */
    bool IsInverted;            /**< Active-low signal indicator.
                                    Inputs: polarity inversion register is set (hardware).
                                    Outputs: the API caller must negate the logic level. */
    bool IsLatched;             /**< Input latch enable.
                                    When true the input value is captured at the interrupt edge
                                    and held until the INPUT register is read (PCAL6416AHF_ReadIntStatus).
                                    Use for mechanically bouncing signals such as card-detect
                                    switches to avoid metastable reads. */
} PCAL6416_IO_Conf_t;

/** @brief Port expander device instance.
 */
typedef struct {
    bool IsInitialized;             /**< True after successful initialization. */
    i2c_master_dev_handle_t Handle; /**< I2C device handle. */
} PCAL6416AHF_Dev_t;

/** @brief              Initialize a PCAL6416AHF port expander instance.
 *                      Registers the device on the I2C bus at the given address, applies the
 *                      supplied pin configuration (direction, polarity, pull resistors, input
 *                      latch, interrupt mask), clears any pending INT_STATUS bits, and
 *                      anchors the internal change-detection snapshot by reading INPUT0/1.
 *  @note               Must be called before any other PCAL6416AHF API functions.
 *                      The pin configuration array must remain valid only for the duration
 *                      of this call; it is not referenced afterwards.
 *  @param p_Bus_Handle Pointer to the I2C master bus handle
 *  @param Address      7-bit I2C address of the device
 *  @param p_Config     Pointer to an array of PCAL6416_IO_Conf_t pin descriptors
 *  @param Count        Number of entries in p_Config
 *  @param p_Device     Pointer to the device instance to initialize
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if any pointer is NULL
 *                      ESP_FAIL if I2C bus registration or register configuration fails
 */
esp_err_t PCAL6416AHF_Init(i2c_master_bus_handle_t *p_Bus_Handle, uint8_t Address,
                           const PCAL6416_IO_Conf_t *p_Config, size_t Count,
                           PCAL6416AHF_Dev_t *p_Device);

/** @brief              Deinitialize a PCAL6416AHF port expander instance and release its I2C handle.
 *  @param p_Device     Pointer to the device instance
 *  @return             ESP_OK on success
 *                      ESP_FAIL if removing the I2C device fails
 */
esp_err_t PCAL6416AHF_Deinit(PCAL6416AHF_Dev_t *p_Device);

/** @brief              Read the logical level of a single input pin.
 *                      Reads INPUT0 or INPUT1 (depending on Port) and extracts the bit at Pin.
 *                      The value reflects hardware polarity inversion if it was configured
 *                      during PCAL6416AHF_Init().
 *  @note               For latched pins, this read clears the hardware latch and updates the
 *                      chip's change-detection reference.  Avoid calling this from an interrupt
 *                      handler context.
 *  @param p_Device     Pointer to the device instance
 *  @param Port         Target port (PCAL6416_PORT_0 or PCAL6416_PORT_1)
 *  @param Pin          Pin number within the port (0-7)
 *  @param p_Level      Pointer to store the pin level (true = high, false = low)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Device or p_Level is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCAL6416AHF_ReadPin(PCAL6416AHF_Dev_t *p_Device, PCAL6416_Port_t Port, uint8_t Pin,
                              bool *p_Level);

/** @brief              Set the logical level of a single output pin.
 *                      Performs a read-modify-write on OUTPUT0 or OUTPUT1.
 *  @param p_Device     Pointer to the device instance
 *  @param Port         Target port (PCAL6416_PORT_0 or PCAL6416_PORT_1)
 *  @param Pin          Pin number within the port (0-7)
 *  @param Level        Desired output level (true = high, false = low)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Device is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCAL6416AHF_WritePin(PCAL6416AHF_Dev_t *p_Device, PCAL6416_Port_t Port, uint8_t Pin,
                               bool Level);

/** @brief              Read both INPUT registers in a single burst transfer.
 *                      Writes the INPUT0 register address once and reads two bytes (INPUT0, INPUT1)
 *                      in the same I2C transaction. This is the preferred way to poll all input
 *                      pins without saturating the bus.
 *  @note               For latched input pins, this read clears the hardware latch and updates
 *                      the chip's change-detection reference - avoid using this interleaved with
 *                      PCAL6416AHF_ReadIntStatus on the same device.
 *  @param p_Device     Pointer to the device instance
 *  @param p_Input0     Pointer to store raw INPUT0 byte (Port 0, one bit per pin)
 *  @param p_Input1     Pointer to store raw INPUT1 byte (Port 1, one bit per pin)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if any pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCAL6416AHF_ReadInputs(PCAL6416AHF_Dev_t *p_Device, uint8_t *p_Input0, uint8_t *p_Input1);

/** @brief              Read and clear both INT_STATUS registers and both INPUT registers in one call.
 *                      The INT_STATUS registers are read first, which clears all pending bits and
 *                      releases INT# (open-drain, returns high).  The INPUT registers are then
 *                      read to refresh the chip's change-detection snapshot and to release any
 *                      active input latches.
 *  @note               For latched input pins the Input values reflect the captured stable state
 *                      at interrupt time, not a later live read.
 *                      Do NOT issue any additional INPUT register reads after this function - a
 *                      subsequent read updates the chip's reference and may prevent detection
 *                      of the reverse transition.
 *  @param p_Device     Pointer to the device instance
 *  @param p_Status0    Pointer to store raw INT_STATUS0 byte (one bit per port-0 pin)
 *  @param p_Status1    Pointer to store raw INT_STATUS1 byte (one bit per port-1 pin)
 *  @param p_Input0     Pointer to store raw INPUT0 byte captured after INT_STATUS clear
 *  @param p_Input1     Pointer to store raw INPUT1 byte captured after INT_STATUS clear
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if any pointer is NULL
 *                      ESP_FAIL if I2C communication fails
 */
esp_err_t PCAL6416AHF_ReadIntStatus(PCAL6416AHF_Dev_t *p_Device,
                                    uint8_t *p_Status0, uint8_t *p_Status1,
                                    uint8_t *p_Input0, uint8_t *p_Input1);

#endif /* PCAL6416AHF_H_ */
