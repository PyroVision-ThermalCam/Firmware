/*
 * networkManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network Manager implementation.
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
#include <esp_netif.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <string.h>

#include "networkTypes.h"
#include "Server/server.h"
#include "Provisioning/provisioning.h"

#include "networkManager.h"

ESP_EVENT_DEFINE_BASE(NETWORK_EVENTS);

#define NVS_NAMESPACE           "wifi_creds"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "password"

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_STARTED_BIT        BIT2

typedef struct {
    bool isInitialized;
    Network_State_t State;
    esp_netif_t *STA_NetIF;
    esp_netif_t *AP_NetIF;
    EventGroupHandle_t EventGroup;
    Network_WiFi_STA_Config_t *STA_Config;
    uint8_t RetryCount;
    esp_netif_ip_info_t IP_Info;
} Network_Manager_State_t;

static Network_Manager_State_t _Network_Manager_State;

static const char *TAG = "Network-Manager";

/** @brief                  WiFi event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_WiFi_Event(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case WIFI_EVENT_STA_START: {
            ESP_LOGD(TAG, "WiFi STA started");

            xEventGroupSetBits(_Network_Manager_State.EventGroup, WIFI_STARTED_BIT);
            esp_wifi_connect();

            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
            ESP_LOGD(TAG, "Connected to AP");

            _Network_Manager_State.RetryCount = 0;
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_WIFI_CONNECTED, NULL, 0, portMAX_DELAY);

            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *Event = (wifi_event_sta_disconnected_t *)p_Data;

            /* Decode disconnect reason for better debugging */
            const char *ReasonStr = "Unknown";
            switch (Event->reason) {
                case WIFI_REASON_UNSPECIFIED:
                    ReasonStr = "Unspecified";
                    break;
                case WIFI_REASON_AUTH_EXPIRE:
                    ReasonStr = "Auth expired";
                    break;
                case WIFI_REASON_AUTH_LEAVE:
                    ReasonStr = "Auth leave";
                    break;
                case WIFI_REASON_ASSOC_EXPIRE:
                    ReasonStr = "Assoc expired";
                    break;
                case WIFI_REASON_ASSOC_TOOMANY:
                    ReasonStr = "Too many assocs";
                    break;
                case WIFI_REASON_NOT_AUTHED:
                    ReasonStr = "Not authenticated";
                    break;
                case WIFI_REASON_NOT_ASSOCED:
                    ReasonStr = "Not associated";
                    break;
                case WIFI_REASON_ASSOC_LEAVE:
                    ReasonStr = "Assoc leave";
                    break;
                case WIFI_REASON_ASSOC_NOT_AUTHED:
                    ReasonStr = "Assoc not authed";
                    break;
                case WIFI_REASON_DISASSOC_PWRCAP_BAD:
                    ReasonStr = "Bad power capability";
                    break;
                case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
                    ReasonStr = "Bad supported channels";
                    break;
                case WIFI_REASON_IE_INVALID:
                    ReasonStr = "Invalid IE";
                    break;
                case WIFI_REASON_MIC_FAILURE:
                    ReasonStr = "MIC failure";
                    break;
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                    ReasonStr = "4-way handshake timeout";
                    break;
                case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
                    ReasonStr = "Group key update timeout";
                    break;
                case WIFI_REASON_IE_IN_4WAY_DIFFERS:
                    ReasonStr = "IE in 4-way differs";
                    break;
                case WIFI_REASON_GROUP_CIPHER_INVALID:
                    ReasonStr = "Invalid group cipher";
                    break;
                case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
                    ReasonStr = "Invalid pairwise cipher";
                    break;
                case WIFI_REASON_AKMP_INVALID:
                    ReasonStr = "Invalid AKMP";
                    break;
                case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
                    ReasonStr = "Unsupported RSN IE version";
                    break;
                case WIFI_REASON_INVALID_RSN_IE_CAP:
                    ReasonStr = "Invalid RSN IE cap";
                    break;
                case WIFI_REASON_802_1X_AUTH_FAILED:
                    ReasonStr = "802.1X auth failed";
                    break;
                case WIFI_REASON_CIPHER_SUITE_REJECTED:
                    ReasonStr = "Cipher suite rejected";
                    break;
                case WIFI_REASON_BEACON_TIMEOUT:
                    ReasonStr = "Beacon timeout";
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    ReasonStr = "No AP found";
                    break;
                case WIFI_REASON_AUTH_FAIL:
                    ReasonStr = "Auth failed";
                    break;
                case WIFI_REASON_ASSOC_FAIL:
                    ReasonStr = "Assoc failed";
                    break;
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    ReasonStr = "Handshake timeout";
                    break;
                case WIFI_REASON_CONNECTION_FAIL:
                    ReasonStr = "Connection failed";
                    break;
                default:
                    break;
            }

            ESP_LOGW(TAG, "Disconnected from AP, reason: %d (%s)", Event->reason, ReasonStr);

            /* Special handling for "No AP found" */
            if (Event->reason == WIFI_REASON_NO_AP_FOUND) {
                ESP_LOGW(TAG, "AP '%s' not found - check SSID spelling, signal strength, or if AP is on 2.4GHz band",
                         _Network_Manager_State.STA_Config->Credentials.SSID);
            }

            _Network_Manager_State.State = NETWORK_STATE_DISCONNECTED;
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_WIFI_DISCONNECTED, p_Data, sizeof(wifi_event_sta_disconnected_t),
                           portMAX_DELAY);

            if (_Network_Manager_State.RetryCount < _Network_Manager_State.STA_Config->MaxRetries) {
                ESP_LOGD(TAG, "Retry %d/%d", _Network_Manager_State.RetryCount++, _Network_Manager_State.STA_Config->MaxRetries);

                vTaskDelay(_Network_Manager_State.STA_Config->RetryInterval / portTICK_PERIOD_MS);
                esp_wifi_connect();
                _Network_Manager_State.State = NETWORK_STATE_CONNECTING;
            } else {
                ESP_LOGE(TAG, "Max retries reached!");

                xEventGroupSetBits(_Network_Manager_State.EventGroup, WIFI_FAIL_BIT);
                _Network_Manager_State.State = NETWORK_STATE_ERROR;
            }

            break;
        }
        case WIFI_EVENT_AP_START: {
            ESP_LOGD(TAG, "WiFi AP started");

            _Network_Manager_State.State = NETWORK_STATE_AP_STARTED;
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_AP_STARTED, NULL, 0, portMAX_DELAY);

            break;
        }
        case WIFI_EVENT_AP_STOP: {
            ESP_LOGD(TAG, "WiFi AP stopped");

            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_AP_STOPPED, NULL, 0, portMAX_DELAY);

            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *Event = (wifi_event_ap_staconnected_t *)p_Data;
            Network_Event_STA_Info_t StaInfo;

            ESP_LOGD(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(Event->mac), Event->aid);
            memcpy(StaInfo.MAC, Event->mac, 6);
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_AP_STA_CONNECTED, &StaInfo, sizeof(StaInfo), portMAX_DELAY);

            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *Event = (wifi_event_ap_stadisconnected_t *)p_Data;
            Network_Event_STA_Info_t StaInfo;

            ESP_LOGD(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(Event->mac), Event->aid);
            memcpy(StaInfo.MAC, Event->mac, 6);
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_AP_STA_DISCONNECTED, &StaInfo, sizeof(StaInfo), portMAX_DELAY);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  IP event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_IP_Event(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *Event = (ip_event_got_ip_t *)p_Data;
            Network_IP_Info_t IP_Data;

            ESP_LOGD(TAG, "Got IP: " IPSTR, IP2STR(&Event->ip_info.ip));

            memcpy(&_Network_Manager_State.IP_Info, &Event->ip_info, sizeof(esp_netif_ip_info_t));
            _Network_Manager_State.State = NETWORK_STATE_CONNECTED;

            IP_Data.IP = Event->ip_info.ip.addr;
            IP_Data.Netmask = Event->ip_info.netmask.addr;
            IP_Data.Gateway = Event->ip_info.gw.addr;
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_WIFI_GOT_IP, &IP_Data, sizeof(IP_Data), portMAX_DELAY);

            xEventGroupSetBits(_Network_Manager_State.EventGroup, WIFI_CONNECTED_BIT);

            break;
        }
        case IP_EVENT_STA_LOST_IP: {
            ESP_LOGW(TAG, "Lost IP address");

            memset(&_Network_Manager_State.IP_Info, 0, sizeof(esp_netif_ip_info_t));

            break;
        }
        default: {
            break;
        }
    }
}

esp_err_t NetworkManager_Init(Network_WiFi_STA_Config_t *p_Config)
{
    esp_err_t Error;

    if (_Network_Manager_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (p_Config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Initializing WiFi Manager");

    memset(&_Network_Manager_State, 0, sizeof(Network_Manager_State_t));

    ESP_ERROR_CHECK(esp_netif_init());

    _Network_Manager_State.EventGroup = xEventGroupCreate();
    if (_Network_Manager_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    _Network_Manager_State.STA_Config = p_Config;

    /* Create network interfaces */
    _Network_Manager_State.STA_NetIF = esp_netif_create_default_wifi_sta();
    if (_Network_Manager_State.STA_NetIF == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif!");

        vEventGroupDelete(_Network_Manager_State.EventGroup);
        _Network_Manager_State.EventGroup = NULL;

        return ESP_FAIL;
    }

    _Network_Manager_State.AP_NetIF = esp_netif_create_default_wifi_ap();
    if (_Network_Manager_State.AP_NetIF == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif!");

        esp_netif_destroy(_Network_Manager_State.STA_NetIF);
        _Network_Manager_State.STA_NetIF = NULL;
        vEventGroupDelete(_Network_Manager_State.EventGroup);
        _Network_Manager_State.EventGroup = NULL;

        return ESP_FAIL;
    }

    /* Initialize WiFi with default configuration */
    wifi_init_config_t WifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    Error = esp_wifi_init(&WifiInitConfig);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %d!", Error);

        esp_netif_destroy(_Network_Manager_State.AP_NetIF);
        esp_netif_destroy(_Network_Manager_State.STA_NetIF);
        vEventGroupDelete(_Network_Manager_State.EventGroup);

        _Network_Manager_State.AP_NetIF = NULL;
        _Network_Manager_State.STA_NetIF = NULL;
        _Network_Manager_State.EventGroup = NULL;

        return Error;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &on_WiFi_Event,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &on_IP_Event,
                                                        NULL,
                                                        NULL));

    /* Set storage type */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    _Network_Manager_State.isInitialized = true;
    _Network_Manager_State.State = NETWORK_STATE_IDLE;

    ESP_LOGD(TAG, "Network Manager initialized");

    return ESP_OK;
}

void NetworkManager_Deinit(void)
{
    if (_Network_Manager_State.isInitialized == false) {
        return;
    }

    ESP_LOGD(TAG, "Deinitializing Network Manager");

    NetworkManager_Stop();
    esp_wifi_deinit();

    if (_Network_Manager_State.STA_NetIF) {
        esp_netif_destroy(_Network_Manager_State.STA_NetIF);
        _Network_Manager_State.STA_NetIF = NULL;
    }

    if (_Network_Manager_State.AP_NetIF) {
        esp_netif_destroy(_Network_Manager_State.AP_NetIF);
        _Network_Manager_State.AP_NetIF = NULL;
    }

    if (_Network_Manager_State.EventGroup) {
        vEventGroupDelete(_Network_Manager_State.EventGroup);
        _Network_Manager_State.EventGroup = NULL;
    }

    _Network_Manager_State.isInitialized = false;
    _Network_Manager_State.State = NETWORK_STATE_IDLE;
}

esp_err_t NetworkManager_StartSTA(void)
{
    wifi_config_t WifiConfig;
    esp_err_t Error;

    if (_Network_Manager_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    _Network_Manager_State.RetryCount = 0;

    xEventGroupClearBits(_Network_Manager_State.EventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_LOGI(TAG, "Starting WiFi in STA mode");
    ESP_LOGI(TAG, "Connecting to SSID: %s", _Network_Manager_State.STA_Config->Credentials.SSID);

    memset(&WifiConfig, 0, sizeof(wifi_config_t));
    WifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    WifiConfig.sta.pmf_cfg.capable = true;
    WifiConfig.sta.pmf_cfg.required = false;
    strncpy((char *)WifiConfig.sta.ssid, _Network_Manager_State.STA_Config->Credentials.SSID,
            sizeof(WifiConfig.sta.ssid) - 1);
    strncpy((char *)WifiConfig.sta.password, _Network_Manager_State.STA_Config->Credentials.Password,
            sizeof(WifiConfig.sta.password) - 1);

    /* Set STA mode - WiFi might still be in APSTA mode from provisioning */
    Error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %d!", Error);

        return Error;
    }

    Error = esp_wifi_set_config(WIFI_IF_STA, &WifiConfig);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %d!", Error);

        return Error;
    }

    /* Start WiFi if not already running */
    Error = esp_wifi_start();
    if ((Error != ESP_OK) && (Error != ESP_ERR_WIFI_STATE)) {
        ESP_LOGE(TAG, "Failed to start WiFi: %d!", Error);

        return Error;
    }

    ESP_LOGI(TAG, "WiFi started, initiating connection...");

    /* Explicitly connect - STA_START event may not fire if WiFi was already running */
    Error = esp_wifi_connect();
    if ((Error != ESP_OK) && (Error != ESP_ERR_WIFI_CONN)) {
        ESP_LOGW(TAG, "esp_wifi_connect returned: %d!", Error);
    }

    _Network_Manager_State.State = NETWORK_STATE_CONNECTING;

    ESP_LOGI(TAG, "WiFi connection initiated");

    return ESP_OK;
}

esp_err_t NetworkManager_StartServer(Network_Server_Config_t *p_Config)
{
    esp_err_t Error;

    Error = Server_Init(p_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize server: %d!", Error);
        return Error;
    }

    Error = Server_Start();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %d!", Error);
        return Error;
    }

    return ESP_OK;
}

esp_err_t NetworkManager_Stop(void)
{
    if (_Network_Manager_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    SNTP_Deinit();

    esp_wifi_stop();

    _Network_Manager_State.State = NETWORK_STATE_IDLE;

    return ESP_OK;
}

esp_err_t NetworkManager_ConnectWiFi(const char *p_SSID, const char *p_Password)
{
    if (_Network_Manager_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (p_SSID != NULL) {
        strncpy(_Network_Manager_State.STA_Config->Credentials.SSID, p_SSID,
                sizeof(_Network_Manager_State.STA_Config->Credentials.SSID) - 1);
    }

    if (p_Password != NULL) {
        strncpy(_Network_Manager_State.STA_Config->Credentials.Password, p_Password,
                sizeof(_Network_Manager_State.STA_Config->Credentials.Password) - 1);
    }

    /* Restart the WiFi connection with the new credentials */
    NetworkManager_Stop();
    NetworkManager_StartSTA();

    /* Wait for connection */
    EventBits_t Bits = xEventGroupWaitBits(_Network_Manager_State.EventGroup,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           30000 / portTICK_PERIOD_MS);

    if (Bits & WIFI_CONNECTED_BIT) {
        ESP_LOGD(TAG, "Connected to SSID: %s!", _Network_Manager_State.STA_Config->Credentials.SSID);
        return ESP_OK;
    } else if (Bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s!", _Network_Manager_State.STA_Config->Credentials.SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout!");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t NetworkManager_DisconnectWiFi(void)
{
    if (_Network_Manager_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Disconnecting from WiFi");

    return esp_wifi_disconnect();
}

bool NetworkManager_isConnected(void)
{
    return _Network_Manager_State.State == NETWORK_STATE_CONNECTED;
}

Network_State_t NetworkManager_GetState(void)
{
    return _Network_Manager_State.State;
}

esp_err_t NetworkManager_GetIP(esp_netif_ip_info_t *p_IP)
{
    if (p_IP == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(p_IP, &_Network_Manager_State.IP_Info, sizeof(esp_netif_ip_info_t));

    return ESP_OK;
}

int8_t NetworkManager_GetRSSI(void)
{
    wifi_ap_record_t ApInfo;

    if (NetworkManager_isConnected() == false) {
        return 0;
    }

    if (esp_wifi_sta_get_ap_info(&ApInfo) == ESP_OK) {
        return ApInfo.rssi;
    }

    return 0;
}

esp_err_t NetworkManager_GetMAC(uint8_t *p_MAC)
{
    if (p_MAC == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_wifi_get_mac(WIFI_IF_STA, p_MAC);
}

esp_err_t NetworkManager_SetCredentials(Network_WiFi_Credentials_t *p_Credentials)
{
    if (p_Credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(_Network_Manager_State.STA_Config->Credentials.SSID, p_Credentials->SSID,
            sizeof(_Network_Manager_State.STA_Config->Credentials.SSID) - 1);
    if (strlen(p_Credentials->Password) != 0) {
        strncpy(_Network_Manager_State.STA_Config->Credentials.Password, p_Credentials->Password,
                sizeof(_Network_Manager_State.STA_Config->Credentials.Password) - 1);
    } else {
        _Network_Manager_State.STA_Config->Credentials.Password[0] = '\0';
    }

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_CREDENTIALS_UPDATED, &_Network_Manager_State.STA_Config->Credentials,
                   sizeof(Network_WiFi_Credentials_t), portMAX_DELAY);

    return ESP_OK;
}

uint8_t NetworkManager_GetConnectedStations(void)
{
    wifi_sta_list_t StaList;

    if (esp_wifi_ap_get_sta_list(&StaList) == ESP_OK) {
        return StaList.num;
    }

    return 0;
}
