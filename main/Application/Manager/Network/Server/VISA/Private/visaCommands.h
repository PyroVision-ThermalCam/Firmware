/*
 * visaCommands.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA commands implementation.
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

#ifndef VISA_COMMANDS_H_
#define VISA_COMMANDS_H_

#include <esp_err.h>

#include <stddef.h>

/** @brief Standard SCPI error codes */
#define SCPI_ERROR_NO_ERROR                 0       /**< No error */
#define SCPI_ERROR_COMMAND_ERROR            -100    /**< Command error */
#define SCPI_ERROR_INVALID_CHARACTER        -101    /**< Invalid character */
#define SCPI_ERROR_SYNTAX_ERROR             -102    /**< Syntax error */
#define SCPI_ERROR_INVALID_SEPARATOR        -103    /**< Invalid separator */
#define SCPI_ERROR_DATA_TYPE_ERROR          -104    /**< Data type error */
#define SCPI_ERROR_PARAMETER_NOT_ALLOWED    -108    /**< Parameter not allowed */
#define SCPI_ERROR_MISSING_PARAMETER        -109    /**< Missing parameter */
#define SCPI_ERROR_COMMAND_HEADER_ERROR     -110    /**< Command header error */
#define SCPI_ERROR_UNDEFINED_HEADER         -113    /**< Undefined header */
#define SCPI_ERROR_EXECUTION_ERROR          -200    /**< Execution error */
#define SCPI_ERROR_DATA_OUT_OF_RANGE        -222    /**< Data out of range */
#define SCPI_ERROR_HARDWARE_MISSING         -241    /**< Hardware missing */
#define SCPI_ERROR_HARDWARE_ERROR           -240    /**< Hardware error */
#define SCPI_ERROR_SYSTEM_ERROR             -300    /**< System error */
#define SCPI_ERROR_OUT_OF_MEMORY            -350    /**< Out of memory */
#define SCPI_ERROR_QUERY_ERROR              -400    /**< Query error */

/** @brief          Initialize command handler
 *  @return         ESP_OK on success, error code otherwise
 */
esp_err_t VISACommands_Init(void);

/** @brief          Deinitialize command handler
 *  @return         ESP_OK on success, error code otherwise
 */
esp_err_t VISACommands_Deinit(void);

/** @brief          Execute VISA/SCPI command
 *  @param Command  Command string
 *  @param Response Response buffer
 *  @param MaxLen   Maximum response length
 *  @return         Response length or error code
 */
int VISACommands_Execute(const char *Command, char *Response, size_t MaxLen);

/** @brief          Get last error from error queue
 *  @return         Error code
 */
int VISACommands_GetError(void);

/** @brief          Clear error queue
 */
void VISACommands_ClearErrors(void);

#endif /* VISA_COMMANDS_H_ */
