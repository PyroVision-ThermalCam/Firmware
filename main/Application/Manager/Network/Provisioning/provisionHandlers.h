/*
 * provisionHandlers.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP provisioning endpoint handlers.
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

#ifndef PROVISION_HANDLERS_H_
#define PROVISION_HANDLERS_H_

#include <esp_http_server.h>

/** @brief              Serve provision HTML page.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t Provision_Handler_Root(httpd_req_t *p_Request);

/** @brief              Serve PyroVision logo SVG.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t Provision_Handler_Logo(httpd_req_t *p_Request);

/** @brief              Handle WiFi scan request.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t Provision_Handler_Scan(httpd_req_t *p_Request);

/** @brief              Handle WiFi connect request.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t Provision_Handler_Connect(httpd_req_t *p_Request);

/** @brief              Handle captive portal detection.
 *                      Responds to Android/iOS captive portal checks.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t Provision_Handler_CaptivePortal(httpd_req_t *p_Request);

#endif /* PROVISION_HANDLERS_H_ */
