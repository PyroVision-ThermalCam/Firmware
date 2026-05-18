/*
 * http_handler.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler.
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

#ifndef HTTP_HANDLER_H_
#define HTTP_HANDLER_H_

#include <esp_err.h>
#include <esp_http_server.h>

#include "lepton_palette.h"
#include "../../../SNTP/sntp.h"
#include "../../../../ImageEncoder/imageEncoder.h"

#define HTTP_SERVER_API_BASE_PATH           "/api/v1"
#define HTTP_SERVER_API_KEY_HEADER          "X-API-Key"

typedef struct {
    bool IsInitialized;
    bool IsRunning;
    httpd_handle_t Handle;
    Network_HTTP_Server_Config_t Config;
    ImageEncoder_Raw_t *RawFrame;
    uint32_t RequestCount;
    uint32_t StartTime;
    float LeptonFPA;
    float LeptonAUX;
    float DeviceTemperatureC;
} HTTP_Server_State_t;

/** @brief              Check API key authentication.
 *  @param p_Request    HTTP request handle
 *  @return             true if authenticated
 */
bool HTTP_Server_CheckAuth(httpd_req_t *p_Request);

/** @brief              Send JSON response.
 *  @param p_Request    HTTP request handle
 *  @param p_JSON       JSON object to send
 *  @param StatusCode   HTTP status code
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Server_SendJSON(httpd_req_t *p_Request, cJSON *p_JSON, int StatusCode);

/** @brief              Send error response.
 *  @param p_Request    HTTP request handle
 *  @param StatusCode   HTTP status code
 *  @param p_Message    Error message
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Server_SendError(httpd_req_t *p_Request, int StatusCode, const char *p_Message);

/** @brief              Parse JSON from request body.
 *  @param p_Request    HTTP request handle
 *  @return             cJSON object or NULL on error
 */
cJSON *HTTP_Server_ParseJSON(httpd_req_t *p_Request);

/** @brief              Handler for POST /api/v1/time.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Time(httpd_req_t *p_Request);

/** @brief              Handler for POST /api/v1/settings - updates one or more settings.
 *                      All fields are optional; only keys present in the JSON body are applied.
 *                      Supported keys:
 *                        "palette"              (number) - active color palette index
 *                        "camera.index"         (number) - active camera index
 *                        "system.image_format"  (number) - ImageEncoder_Format_t value
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Settings(httpd_req_t *p_Request);

/** @brief              Handler for GET /api/v1/telemetry.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Telemetry(httpd_req_t *p_Request);

/** @brief              Handler for GET /api/v1/info - returns device information and capabilities.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Info(httpd_req_t *p_Request);

/** @brief              Handler for GET /api/v1/memory.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Memory(httpd_req_t *p_Request);

/** @brief              Handler for GET /api/v1/image.
 *  @param p_Request    HTTP request handle
 *  @return             ESP_OK on success
 */
esp_err_t HTTP_Handler_Image(httpd_req_t *p_Request);

#endif /* HTTP_HANDLER_H_ */