/*
 * provisioning.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WiFi provisioning implementation using ESP-IDF unified provisioning.
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
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include <string.h>

#include "../networkManager.h"
#include "../Server/server.h"
#include "../DNS/dnsServer.h"
#include "provisioning.h"
#include "Settings/settingsManager.h"

typedef struct {
    bool isInitialized;
    bool isActive;
    bool usePortal;
    char Name[32];
    uint32_t TimeOut;
    esp_timer_handle_t TimeoutTimer;
    wifi_sta_config_t WiFi_STA_Config;
    bool hasCredentials;
} Provisioning_State_t;

static Provisioning_State_t _Provisioning_State;

static const char *TAG = "Provisioning";

/** @brief        Timeout timer callback.
 *  @param p_Arg  Timer argument
 */
static void on_TimeoutTimer_Handler(void *p_Arg)
{
    ESP_LOGD(TAG, "Provisioning timeout reached");
    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_TIMEOUT, NULL, 0, pdMS_TO_TICKS(100));
}

/** @brief              Provisioning event handler.
 *  @param p_Arg        User argument
 *  @param event_base   Event base
 *  @param event_id     Event ID
 *  @param event_data   Event data
 */
static void on_Prov_Event(void *p_Arg, esp_event_base_t EventBase, int32_t EventId, void *p_EventData)
{
    switch (EventId) {
        case WIFI_PROV_START: {
            ESP_LOGD(TAG, "Provisioning started");

            break;
        }
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_cfg = static_cast<wifi_sta_config_t *>(p_EventData);

            if (wifi_cfg != NULL) {
                memcpy(&_Provisioning_State.WiFi_STA_Config.ssid, wifi_cfg->ssid, sizeof(wifi_cfg->ssid));
                memcpy(&_Provisioning_State.WiFi_STA_Config.password, wifi_cfg->password, sizeof(wifi_cfg->password));

                _Provisioning_State.hasCredentials = true;

                ESP_LOGD(TAG, "Received WiFi credentials - SSID: %s", _Provisioning_State.WiFi_STA_Config.ssid);
            } else {
                ESP_LOGE(TAG, "WiFi config in CRED_RECV is NULL!");
            }

            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = static_cast<wifi_prov_sta_fail_reason_t *>(p_EventData);

            ESP_LOGE(TAG, "Provisioning failed! Reason: %s", (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Auth Error" : "AP Not Found");

            break;
        }
        case WIFI_PROV_CRED_SUCCESS: {
            Settings_WiFi_t WiFiSettings;

            ESP_LOGD(TAG, "Provisioning successful");

            if (_Provisioning_State.TimeoutTimer != NULL) {
                esp_timer_stop(_Provisioning_State.TimeoutTimer);
            }

            /* Copy SSID and password from saved configuration */
            if (_Provisioning_State.hasCredentials) {
                SettingsManager_GetWiFi(&WiFiSettings);

                strncpy(WiFiSettings.SSID, (const char *)_Provisioning_State.WiFi_STA_Config.ssid, sizeof(WiFiSettings.SSID) - 1);
                strncpy(WiFiSettings.Password, (const char *)_Provisioning_State.WiFi_STA_Config.password,
                        sizeof(WiFiSettings.Password) - 1);
                
                SettingsManager_UpdateWiFi(&WiFiSettings);
            } else {
                ESP_LOGE(TAG, "No WiFi credentials available!");
            }

            break;
        }
        case WIFI_PROV_END: {
            ESP_LOGD(TAG, "Provisioning ended");

            wifi_prov_mgr_deinit();

            _Provisioning_State.isActive = false;

            break;
        }
        default: {
            break;
        }
    }
}

esp_err_t Provisioning_Init(void)
{
    Settings_Provisioning_t ProvisioningSettings;

    if (_Provisioning_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    SettingsManager_GetProvisioning(&ProvisioningSettings);

    ESP_LOGD(TAG, "Initializing Provisioning Manager");
    strncpy(_Provisioning_State.Name, ProvisioningSettings.Name,
            sizeof(_Provisioning_State.Name) - 1);
    _Provisioning_State.TimeOut = ProvisioningSettings.Timeout;
    _Provisioning_State.usePortal = true;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &on_Prov_Event, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = on_TimeoutTimer_Handler,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "prov_timeout",
        .skip_unhandled_events = false
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &_Provisioning_State.TimeoutTimer));

    _Provisioning_State.isInitialized = true;

    ESP_LOGD(TAG, "Provisioning initialized");

    return ESP_OK;
}

void Provisioning_Deinit(void)
{
    if (_Provisioning_State.isInitialized == false) {
        return;
    }

    Provisioning_Stop();

    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_prov_mgr_deinit();

    if (_Provisioning_State.TimeoutTimer != NULL) {
        esp_timer_delete(_Provisioning_State.TimeoutTimer);
        _Provisioning_State.TimeoutTimer = NULL;
    }

    esp_event_handler_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &on_Prov_Event);

    _Provisioning_State.isInitialized = false;
}

esp_err_t Provisioning_Start(void)
{
    esp_err_t Error;
    uint8_t MAC[6];
    wifi_config_t Config;
    Network_HTTP_Server_Config_t ServerConfig;
    esp_netif_t *NetIf;

    if (_Provisioning_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_Provisioning_State.isActive) {
        ESP_LOGW(TAG, "Provisioning already active");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Starting captive portal provisioning");

    NetworkManager_GetMAC(MAC);

    /* Create unique SSID */
    snprintf(reinterpret_cast<char *>(Config.ap.ssid), sizeof(Config.ap.ssid),
             "%.26s-%02X%02X", _Provisioning_State.Name, MAC[4], MAC[5]);
    Config.ap.ssid_len = strlen(reinterpret_cast<char *>(Config.ap.ssid));
    Config.ap.channel = 1;
    Config.ap.authmode = WIFI_AUTH_OPEN;
    Config.ap.max_connection = 4;

    ESP_LOGD(TAG, "Starting SoftAP with SSID: %s", Config.ap.ssid);

    /* Set APSTA mode to allow WiFi scanning while AP is active */
    Error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APSTA mode: %d!", Error);

        return Error;
    }

    Error = esp_wifi_set_config(WIFI_IF_AP, &Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %d!", Error);

        return Error;
    }

    Error = esp_wifi_start();
    if ((Error != ESP_OK) && (Error != ESP_ERR_WIFI_STATE)) {
        ESP_LOGE(TAG, "Failed to start WiFi: %d!", Error);

        return Error;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    /* Start HTTP server with minimal configuration for provisioning */
    ServerConfig.Port = 80;
    ServerConfig.MaxClients = 1;  /* Only one client for provisioning */
    ServerConfig.EnableCORS = true;
    memset(ServerConfig.API_Key, '\0', sizeof(ServerConfig.API_Key));

    /* Initialize HTTP server first (without WebSocket) */
    Error = HTTP_Server_Init(&ServerConfig);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init HTTP server: 0x%x!", Error);

        esp_wifi_stop();

        return Error;
    }

    Error = HTTP_Server_Start();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: 0x%x!", Error);

        HTTP_Server_Deinit();
        esp_wifi_stop();

        return Error;
    }

    /* Start DNS server for captive portal */
    Error = DNS_Server_Start();
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start DNS server: %d!", Error);
        /* Continue anyway, DNS is not critical */
    }

    _Provisioning_State.isActive = true;
    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STARTED, NULL, 0, portMAX_DELAY);

    /* Start timeout timer */
    if (_Provisioning_State.TimeoutTimer != NULL) {
        esp_timer_start_once(_Provisioning_State.TimeoutTimer, _Provisioning_State.TimeOut * 1000000ULL);
    }

    NetIf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (NetIf != NULL) {
        esp_netif_ip_info_t IP;

        if (esp_netif_get_ip_info(NetIf, &IP) == ESP_OK) {
            ESP_LOGD(TAG, "Captive portal started at http://" IPSTR, IP2STR(&IP.ip));
        } else {
            ESP_LOGD(TAG, "Captive portal started at http://192.168.4.1");
        }
    } else {
        ESP_LOGD(TAG, "Captive portal started at http://192.168.4.1");
    }

    return ESP_OK;
}

esp_err_t Provisioning_Stop(void)
{
    if (_Provisioning_State.isActive == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Provisioning");

    _Provisioning_State.isActive = false;

    if (_Provisioning_State.usePortal) {
        ESP_LOGD(TAG, "Stopping DNS server");
        DNS_Server_Stop();

        ESP_LOGD(TAG, "Stopping HTTP server");
        HTTP_Server_Stop();

        ESP_LOGD(TAG, "Stopping WiFi");
        esp_wifi_stop();

        /* Small delay for WiFi to fully stop */
        vTaskDelay(pdMS_TO_TICKS(50));

        HTTP_Server_Deinit();

        ESP_LOGD(TAG, "Provisioning stopped");
    } else {
        if (_Provisioning_State.TimeoutTimer != NULL) {
            esp_timer_stop(_Provisioning_State.TimeoutTimer);
        }

        wifi_prov_mgr_stop_provisioning();
    }

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STOPPED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

bool Provisioning_isProvisioned(void)
{
    bool isProvisioned;
    wifi_prov_mgr_config_t Config;

    memset(&Config, 0, sizeof(wifi_prov_mgr_config_t));

    Config.scheme = wifi_prov_scheme_softap;
    Config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;

    if (wifi_prov_mgr_init(Config) == ESP_OK) {
        wifi_prov_mgr_is_provisioned(&isProvisioned);
        wifi_prov_mgr_deinit();
    }

    ESP_LOGD(TAG, "Provisioned: %s", isProvisioned ? "true" : "false");

    return isProvisioned;
}

esp_err_t Provisioning_Reset(void)
{
    ESP_LOGD(TAG, "Resetting Provisioning");

    wifi_prov_mgr_reset_provisioning();

    return ESP_OK;
}

bool Provisioning_IsActive(void)
{
    return _Provisioning_State.isActive;
}