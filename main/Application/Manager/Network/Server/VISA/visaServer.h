/*
 * visaServer.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: VISA server implementation.
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

#ifndef VISA_SERVER_H_
#define VISA_SERVER_H_

#include <esp_err.h>

#include "../../networkTypes.h"

/** @brief VISA error codes */
typedef enum {
    VISA_OK = 0,                        /**< Operation successful */
    VISA_ERR_INVALID_ARG = -1,          /**< Invalid argument */
    VISA_ERR_NO_MEM = -2,               /**< Out of memory */
    VISA_ERR_SOCKET = -3,               /**< Socket error */
    VISA_ERR_TIMEOUT = -4,              /**< Operation timeout */
    VISA_ERR_INVALID_COMMAND = -5,      /**< Invalid command */
    VISA_ERR_EXECUTION = -6,            /**< Command execution error */
    VISA_ERR_NOT_INITIALIZED = -7,      /**< Server not initialized */
} VISA_Error_t;

/** @brief          Initialize VISA server.
 *  @return         VISA_OK on success, error code otherwise
 */
esp_err_t VISAServer_Init(void);

/** @brief  Deinitialize VISA server.
 *  @return VISA_OK on success, error code otherwise
 */
esp_err_t VISAServer_Deinit(void);

/** @brief  Check if VISA server is running
 *  @return true if running, false otherwise
 */
bool VISAServer_IsRunning(void);

/** @brief  Start VISA server.
 *  @return VISA_OK on success, error code otherwise
 */
esp_err_t VISAServer_Start(void);

/** @brief  Stop VISA server.
 *  @return VISA_OK on success, error code otherwise
 */
esp_err_t VISAServer_Stop(void);

#endif /* VISA_SERVER_H_ */
