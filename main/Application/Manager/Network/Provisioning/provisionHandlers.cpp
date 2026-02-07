/*
 * provisionHandlers.cpp
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

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_http_server.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cJSON.h>
#include <string.h>

#include "provisionHandlers.h"
#include "../networkTypes.h"
#include "../networkManager.h"
#include "../../Settings/settingsManager.h"

static const char *TAG = "ProvisionHandlers";

/* Embed HTML file */
extern const uint8_t provision_html_start[] asm("_binary_provision_html_start");
extern const uint8_t provision_html_end[] asm("_binary_provision_html_end");

/* Embed logo PNG */
extern const uint8_t logo_png_start[] asm("_binary_logo_png_start");
extern const uint8_t logo_png_end[] asm("_binary_logo_png_end");

/** @brief              Send JSON response.
 *  @param p_Request    HTTP request handle
 *  @param p_JSON       JSON object to send
 *  @param StatusCode   HTTP status code
 *  @return             ESP_OK on success
 */
static esp_err_t send_JSON_Response(httpd_req_t *p_Request, cJSON *p_JSON, int StatusCode)
{
    esp_err_t Error;

    char *ResponseStr = cJSON_PrintUnformatted(p_JSON);
    if (ResponseStr == NULL) {
        httpd_resp_send_500(p_Request);
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_status(p_Request, StatusCode == 200 ? HTTPD_200 : HTTPD_400);
    httpd_resp_set_type(p_Request, "application/json");
    httpd_resp_set_hdr(p_Request, "Access-Control-Allow-Origin", "*");

    Error = httpd_resp_send(p_Request, ResponseStr, strlen(ResponseStr));

    free(ResponseStr);
    cJSON_Delete(p_JSON);

    return Error;
}

esp_err_t Provision_Handler_Root(httpd_req_t *p_Request)
{
    httpd_resp_set_type(p_Request, "text/html");
    httpd_resp_set_hdr(p_Request, "Cache-Control", "no-cache");

    return httpd_resp_send(p_Request, (const char *)provision_html_start, provision_html_end - provision_html_start);
}

esp_err_t Provision_Handler_Logo(httpd_req_t *p_Request)
{
    httpd_resp_set_type(p_Request, "image/png+xml");
    httpd_resp_set_hdr(p_Request, "Cache-Control", "public, max-age=86400");

    return httpd_resp_send(p_Request, (const char *)logo_png_start, logo_png_end - logo_png_start);
}

esp_err_t Provision_Handler_CaptivePortal(httpd_req_t *p_Request)
{
    char address[26] = {0};
    esp_netif_t *ap_netif;

    ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif != NULL) {
        esp_netif_ip_info_t ip_info;

        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            snprintf((char *)address, sizeof(address) - 1, "http://" IPSTR, IP2STR(&ip_info.ip));
        } else {
            strncpy((char *)address, "http://192.168.4.1", sizeof(address) - 1);
        }
    } else {
        strncpy((char *)address, "http://192.168.4.1", sizeof(address) - 1);
    }

    ESP_LOGD(TAG, "Captive portal redirect to %s", address);

    /* Redirect captive portal detection to main page */
    httpd_resp_set_status(p_Request, "302 Found");
    httpd_resp_set_hdr(p_Request, "Location", address);
    httpd_resp_send(p_Request, NULL, 0);

    return ESP_OK;
}

esp_err_t Provision_Handler_Scan(httpd_req_t *p_Request)
{
    wifi_scan_config_t ScanConfig;
    uint16_t APCount = 0;
    esp_err_t Error;

    memset(&ScanConfig, 0, sizeof(ScanConfig));

    /* Start async scan */
    Error = esp_wifi_scan_start(&ScanConfig, false);
    if (Error != ESP_OK) {
        cJSON *Response;

        ESP_LOGW(TAG, "Scan start failed: %d", Error);

        Response = cJSON_CreateObject();
        cJSON_AddArrayToObject(Response, "networks");

        return send_JSON_Response(p_Request, Response, 200);
    }

    /* Wait for scan to complete (max 10 seconds) */
    for (uint8_t i = 0; i < 100; i++) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (esp_wifi_scan_get_ap_num(&APCount) == ESP_OK) {
            break;
        }
    }

    if (APCount == 0) {
        cJSON *Response;

        Response = cJSON_CreateObject();
        cJSON_AddArrayToObject(Response, "networks");
        return send_JSON_Response(p_Request, Response, 200);
    }

    wifi_ap_record_t *APList = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * APCount);
    if (APList == NULL) {
        httpd_resp_send_500(p_Request);
        return ESP_ERR_NO_MEM;
    }

    esp_wifi_scan_get_ap_records(&APCount, APList);

    cJSON *Response = cJSON_CreateObject();
    cJSON *Networks = cJSON_AddArrayToObject(Response, "networks");

    for (uint16_t i = 0; i < APCount; i++) {
        cJSON *Network = cJSON_CreateObject();
        cJSON_AddStringToObject(Network, "ssid", (const char *)APList[i].ssid);
        cJSON_AddNumberToObject(Network, "rssi", APList[i].rssi);
        cJSON_AddNumberToObject(Network, "channel", APList[i].primary);
        cJSON_AddStringToObject(Network, "auth",
                                APList[i].authmode == WIFI_AUTH_OPEN ? "open" : "secured");

        cJSON_AddItemToArray(Networks, Network);
    }

    free(APList);

    return send_JSON_Response(p_Request, Response, 200);
}

esp_err_t Provision_Handler_Connect(httpd_req_t *p_Request)
{
    char *Buffer = NULL;
    cJSON *JSON = NULL;
    cJSON *SSID_JSON = NULL;
    cJSON *Password_JSON = NULL;
    cJSON *Response = NULL;
    esp_err_t Error = ESP_OK;
    int Received;
    Network_WiFi_Credentials_t Credentials;

    int ContentLen = p_Request->content_len;
    if (ContentLen > 512) {
        httpd_resp_send_err(p_Request, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }

    Buffer = (char *)heap_caps_malloc(ContentLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (Buffer == NULL) {
        httpd_resp_send_500(p_Request);
        return ESP_ERR_NO_MEM;
    }

    Received = httpd_req_recv(p_Request, Buffer, ContentLen);
    if (Received <= 0) {
        free(Buffer);
        httpd_resp_send_err(p_Request, HTTPD_400_BAD_REQUEST, "Failed to read request");
        return ESP_FAIL;
    }

    Buffer[Received] = '\0';

    JSON = cJSON_Parse(Buffer);
    free(Buffer);

    if (JSON == NULL) {
        httpd_resp_send_err(p_Request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    SSID_JSON = cJSON_GetObjectItem(JSON, "ssid");
    Password_JSON = cJSON_GetObjectItem(JSON, "password");

    if ((cJSON_IsString(SSID_JSON) == false) || (cJSON_IsString(Password_JSON) == false)) {
        cJSON_Delete(JSON);
        httpd_resp_send_err(p_Request, HTTPD_400_BAD_REQUEST, "Missing ssid or password");
        return ESP_FAIL;
    }

    const char *SSID = SSID_JSON->valuestring;
    const char *Password = Password_JSON->valuestring;

    ESP_LOGI(TAG, "Provisioning request: SSID=%s", SSID);

    strncpy(Credentials.SSID, SSID, sizeof(Credentials.SSID) - 1);
    strncpy(Credentials.Password, Password, sizeof(Credentials.Password) - 1);

    Error = NetworkManager_SetCredentials(&Credentials);

    Response = cJSON_CreateObject();

    if (Error == ESP_OK) {
        App_Settings_WiFi_t WiFiSettings;
        esp_err_t SaveError;

        SettingsManager_GetWiFi(&WiFiSettings);

        strncpy(WiFiSettings.SSID, SSID, sizeof(WiFiSettings.SSID) - 1);
        strncpy(WiFiSettings.Password, Password, sizeof(WiFiSettings.Password) - 1);

        SettingsManager_UpdateWiFi(&WiFiSettings);

        SaveError = SettingsManager_Save();
        if (SaveError == ESP_OK) {
            ESP_LOGI(TAG, "WiFi credentials saved to NVS");

            cJSON_AddBoolToObject(Response, "success", true);
            cJSON_AddStringToObject(Response, "message", "Credentials saved, connecting to WiFi...");

            send_JSON_Response(p_Request, Response, 200);

            /* Post event with short timeout to avoid blocking HTTP task */
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_SUCCESS, NULL, 0, pdMS_TO_TICKS(100));
        } else {
            ESP_LOGE(TAG, "Failed to save credentials to NVS: %d!", SaveError);

            cJSON_AddBoolToObject(Response, "success", false);
            cJSON_AddStringToObject(Response, "error", "NVS storage full, please reset device");
            send_JSON_Response(p_Request, Response, 500);
        }
    } else {
        cJSON_AddBoolToObject(Response, "success", false);
        cJSON_AddStringToObject(Response, "error", "Failed to save credentials");
        send_JSON_Response(p_Request, Response, 400);
    }

    cJSON_Delete(JSON);

    return Error;
}
