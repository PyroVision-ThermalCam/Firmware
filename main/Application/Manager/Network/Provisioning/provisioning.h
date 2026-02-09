/*
 * provisioning.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WiFi provisioning via BLE and SoftAP.
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

#ifndef PROVISIONING_H_
#define PROVISIONING_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../networkTypes.h"

/** @brief          Initialize provisioning manager.
 *  @param p_Config Network provisioning configuration
 *  @return         ESP_OK on success
 */
esp_err_t Provisioning_Init(Network_Provisioning_Config_t *p_Config);

/** @brief Deinitialize provisioning manager.
 */
void Provisioning_Deinit(void);

/** @brief  Start provisioning.
 *  @return ESP_OK on success
 */
esp_err_t Provisioning_Start(void);

/** @brief  Stop provisioning.
 *  @return ESP_OK on success
 */
esp_err_t Provisioning_Stop(void);

/** @brief  Check if device is provisioned.
 *  @return true if provisioned
 */
bool Provisioning_isProvisioned(void);

/** @brief  Reset provisioning (clear stored credentials).
 *  @return ESP_OK on success
 */
esp_err_t Provisioning_Reset(void);

/** @brief  Check if provisioning is active.
 *  @return true if provisioning is running
 */
bool Provisioning_IsActive(void);

#endif /* PROVISIONING_H_ */
