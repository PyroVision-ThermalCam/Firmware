/*
 * http_handler_image.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: HTTP handler implementation for image.
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
#include <esp_timer.h>

#include <cJSON.h>

#include "http_handler.h"
#include "../http_server.h"

extern HTTP_Server_State_t _HTTP_Server_State;

static const char *TAG = "HTTP-Image-Handler";

esp_err_t HTTP_Handler_Image(httpd_req_t *p_Request)
{
    esp_err_t Error;
    ImageEncoder_EncodedImage_t Encoded;
    ImageEncoder_Format_t Format = IMAGE_FORMAT_JPEG;

    _HTTP_Server_State.RequestCount++;

    if (HTTP_Server_CheckAuth(p_Request) == false) {
        return HTTP_Server_SendError(p_Request, 401, "Unauthorized");
    } else if (_HTTP_Server_State.RawFrame == NULL) {
        return HTTP_Server_SendError(p_Request, 503, "No frame data available");
    }

    if (xSemaphoreTake(_HTTP_Server_State.RawFrame->Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return HTTP_Server_SendError(p_Request, 503, "Frame busy");
    }

    if ((_HTTP_Server_State.RawFrame->Width == 0) || (_HTTP_Server_State.RawFrame->Height == 0)) {
        xSemaphoreGive(_HTTP_Server_State.RawFrame->Mutex);
        return HTTP_Server_SendError(p_Request, 503, "No valid frame available");
    }

    Error = ImageEncoder_Encode(_HTTP_Server_State.RawFrame, Format, &Encoded);

    xSemaphoreGive(_HTTP_Server_State.RawFrame->Mutex);

    if (Error != ESP_OK) {
        return HTTP_Server_SendError(p_Request, 500, "Image encoding failed");
    }

    switch (Format) {
        case IMAGE_FORMAT_JPEG: {
            httpd_resp_set_type(p_Request, "image/jpeg");

            break;
        }
        case IMAGE_FORMAT_PNG: {
            httpd_resp_set_type(p_Request, "image/png");

            break;
        }
        case IMAGE_FORMAT_RAW: {
            httpd_resp_set_type(p_Request, "application/octet-stream");

            break;
        }
        case IMAGE_FORMAT_BITMAP: {
            httpd_resp_set_type(p_Request, "image/bmp");

            break;
        }
        default: {
            ImageEncoder_Free(&Encoded);

            return HTTP_Server_SendError(p_Request, 500, "Unknown image format");
        }
    }

    if (_HTTP_Server_State.Config.EnableCORS) {
        httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");
    }

    Error = httpd_resp_send(p_Request, reinterpret_cast<const char *>(Encoded.Data), Encoded.Size);

    ImageEncoder_Free(&Encoded);

    return Error;
}