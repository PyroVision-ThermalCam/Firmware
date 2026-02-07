/*
 * networkTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network task implementation.
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
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>

#include <string.h>

#include "managers.h"
#include "networkTask.h"
#include "Application/Tasks/GUI/guiTask.h"

#define NETWORK_TASK_STOP_REQUEST               BIT0
#define NETWORK_TASK_BROADCAST_FRAME            BIT1
#define NETWORK_TASK_PROV_SUCCESS               BIT2
#define NETWORK_TASK_WIFI_CONNECTED             BIT3
#define NETWORK_TASK_WIFI_DISCONNECTED          BIT4
#define NETWORK_TASK_OPEN_WIFI_REQUEST          BIT5
#define NETWORK_TASK_PROV_TIMEOUT               BIT6
#define NETWORK_TASK_SNTP_TIMEZONE_SET          BIT7
#define NETWORK_TASK_SNTP_TIME_SYNCED           BIT8
#define NETWORK_TASK_WIFI_CREDENTIALS_UPDATED   BIT10
#define LEPTON_SPOTMETER_READY                  BIT11

typedef struct {
    bool isInitialized;
    bool isConnected;
    bool isRunning;
    bool ApplicationStarted;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint32_t StartTime;
    Network_State_t State;
    const char *Timezone;
    App_Context_t *AppContext;
    char SSID[33];
    char Password[65];
    App_Lepton_ROI_Result_t ROIResult;
} Network_Task_State_t;

static Network_Task_State_t _NetworkTask_State;

static const char *TAG = "network_task";

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Lepton_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Lepton event received: ID=%d", ID);

    switch (ID) {
        case LEPTON_EVENT_RESPONSE_SPOTMETER: {
            memcpy(&_NetworkTask_State.ROIResult, p_Data, sizeof(App_Lepton_ROI_Result_t));

            xEventGroupSetBits(_NetworkTask_State.EventGroup, LEPTON_SPOTMETER_READY);

            break;
        }
    }
}

/** @brief                  SNTP event handler for task coordination.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_SNTP_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case SNTP_EVENT_TZ_CHANGED: {
            ESP_LOGD(TAG, "Timezone set");

            _NetworkTask_State.Timezone = static_cast<const char *>(p_Data);

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_SNTP_TIMEZONE_SET);

            break;
        }
        case SNTP_EVENT_SNTP_SYNCED: {
            ESP_LOGD(TAG, "SNTP time synchronized");

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_SNTP_TIME_SYNCED);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Network event handler for task coordination.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case NETWORK_EVENT_CREDENTIALS_UPDATED: {
            Network_WiFi_Credentials_t *Credentials = (Network_WiFi_Credentials_t *)p_Data;

            ESP_LOGD(TAG, "WiFi credentials updated");

            memcpy(&_NetworkTask_State.SSID,
                   Credentials->SSID,
                   sizeof(_NetworkTask_State.SSID));
            memcpy(&_NetworkTask_State.Password,
                   Credentials->Password,
                   sizeof(_NetworkTask_State.Password));

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CREDENTIALS_UPDATED);

            break;
        }
        case NETWORK_EVENT_WIFI_CONNECTED: {
            ESP_LOGD(TAG, "WiFi connected");

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            ESP_LOGD(TAG, "WiFi disconnected");

            _NetworkTask_State.State = NETWORK_STATE_DISCONNECTED;
            _NetworkTask_State.isConnected = false;

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);

            break;
        }
        case NETWORK_EVENT_WIFI_GOT_IP: {
            _NetworkTask_State.State = NETWORK_STATE_CONNECTED;
            _NetworkTask_State.isConnected = true;

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            ESP_LOGD(TAG, "Provisioning success");

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);

            break;
        }
        case NETWORK_EVENT_PROV_FAILED: {
            ESP_LOGE(TAG, "Provisioning failed!");

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            ESP_LOGW(TAG, "Provisioning timeout - stopping provisioning");

            Provisioning_Stop();

            break;
        }
        case NETWORK_EVENT_OPEN_WIFI_REQUEST: {
            ESP_LOGD(TAG, "Open WiFi request received");

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  GUI event handler for task coordination.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case GUI_EVENT_APP_STARTED: {
            ESP_LOGD(TAG, "Application started event received");

            _NetworkTask_State.ApplicationStarted = true;

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief              Network task main loop.
 *  @param p_Parameters Task parameters
 */
static void Task_Network(void *p_Parameters)
{
    App_Settings_WiFi_t WiFiSettings;

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Network task started on core %d", xPortGetCoreID());

    while (_NetworkTask_State.ApplicationStarted == false) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    SettingsManager_GetWiFi(&WiFiSettings);

    ESP_LOGI(TAG, "Autoconnect is %s", (WiFiSettings.AutoConnect) ? "enabled" : "disabled");

    /* If autoconnect is disabled, wait for explicit WiFi open request in main loop */
    bool WaitingForWiFiRequest = (WiFiSettings.AutoConnect == false);

    if (WaitingForWiFiRequest == false) {
        /* Check if WiFi credentials are available */
        if (strlen(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) == 0) {
            ESP_LOGW(TAG, "No credentials found, starting provisioning");

            Provisioning_Start();

            _NetworkTask_State.State = NETWORK_STATE_PROVISIONING;
        } else {
            ESP_LOGD(TAG, "Credentials found, connecting to WiFi");
            NetworkManager_StartSTA();
        }
    } else {
        ESP_LOGD(TAG, "Waiting for WiFi open request...");
        _NetworkTask_State.State = NETWORK_STATE_IDLE;
    }

    while (_NetworkTask_State.isRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_NetworkTask_State.EventGroup);
        if (EventBits & NETWORK_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _NetworkTask_State.isRunning = false;

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

            break;
        } else if (EventBits & NETWORK_TASK_WIFI_CONNECTED) {
            /* Notify Time Manager that network is available */
            TimeManager_OnNetworkConnected();

            /* Ensure we have valid server config */
            if (_NetworkTask_State.AppContext->Server_Config.HTTP_Server.Port == 0) {
                ESP_LOGW(TAG, "Server config port is 0, using default 8080");
                _NetworkTask_State.AppContext->Server_Config.HTTP_Server.Port = 8080;
            }

            if (NetworkManager_StartServer(&_NetworkTask_State.AppContext->Server_Config) == ESP_OK) {
                ESP_LOGD(TAG, "HTTP/WebSocket server started on port %d",
                         _NetworkTask_State.AppContext->Server_Config.HTTP_Server.Port);
                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_STARTED, NULL, 0, portMAX_DELAY);
            } else {
                ESP_LOGE(TAG, "Failed to start HTTP/WebSocket server");
                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_ERROR, NULL, 0, portMAX_DELAY);
            }

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);
        } else if (EventBits & NETWORK_TASK_WIFI_DISCONNECTED) {
            ESP_LOGD(TAG, "Handling WiFi disconnection");

            /* Notify Time Manager that network is unavailable */
            TimeManager_OnNetworkDisconnected();

            /* Only start provisioning if not already active and we have no credentials */
            if ((Provisioning_isActive() == false) && strlen(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) == 0) {
                Provisioning_Start();
            }

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);
        } else if (EventBits & NETWORK_TASK_PROV_SUCCESS) {
            ESP_LOGI(TAG, "Provisioning success - stopping provisioning and connecting to WiFi");

            /* Reset watchdog before potentially long operation */
            esp_task_wdt_reset();

            /* Stop provisioning (HTTP server on port 80 and DNS) */
            Provisioning_Stop();

            ESP_LOGI(TAG, "Provisioning stopped, starting WiFi STA connection");

            /* Reset watchdog after provisioning stop */
            esp_task_wdt_reset();

            /* Connect to WiFi with the new credentials */
            NetworkManager_StartSTA();

            ESP_LOGI(TAG, "WiFi STA connection initiated");

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);
        } else if (EventBits & NETWORK_TASK_PROV_TIMEOUT) {
            ESP_LOGD(TAG, "Handling provisioning timeout");

            esp_task_wdt_reset();
            Provisioning_Stop();
            esp_task_wdt_reset();

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_TIMEOUT);
        } else if (EventBits & NETWORK_TASK_SNTP_TIMEZONE_SET) {
            App_Settings_System_t SystemSettings;

            ESP_LOGD(TAG, "Handling timezone set request");

            SettingsManager_GetSystem(&SystemSettings);

            memcpy(&SystemSettings.Timezone, _NetworkTask_State.Timezone, sizeof(SystemSettings.Timezone));
            TimeManager_SetTimezone(SystemSettings.Timezone);

            SettingsManager_UpdateSystem(&SystemSettings);
            SettingsManager_Save();

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_SNTP_TIMEZONE_SET);
        } else if (EventBits & NETWORK_TASK_OPEN_WIFI_REQUEST) {
            ESP_LOGD(TAG, "Handling WiFi open request");

            /* Start WiFi connection if we were waiting */
            if (WaitingForWiFiRequest) {
                if ((Provisioning_isProvisioned() == false) &&
                    (strlen(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) == 0)) {
                    ESP_LOGW(TAG, "No credentials found, starting provisioning");

                    Provisioning_Start();

                    _NetworkTask_State.State = NETWORK_STATE_PROVISIONING;
                } else {
                    NetworkManager_StartSTA();
                }

                WaitingForWiFiRequest = false;
            }

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);
        } else if (EventBits & NETWORK_TASK_WIFI_CREDENTIALS_UPDATED) {
            App_Settings_WiFi_t WiFiSettings;

            SettingsManager_GetWiFi(&WiFiSettings);

            memcpy(WiFiSettings.SSID, _NetworkTask_State.SSID, sizeof(WiFiSettings.SSID));
            memcpy(WiFiSettings.Password, _NetworkTask_State.Password, sizeof(WiFiSettings.Password));

            SettingsManager_UpdateWiFi(&WiFiSettings);
            SettingsManager_Save();

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CREDENTIALS_UPDATED);
        } else if (EventBits & LEPTON_SPOTMETER_READY) {
            if (Server_isRunning()) {
                
            }
            xEventGroupClearBits(_NetworkTask_State.EventGroup, LEPTON_SPOTMETER_READY);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Network task shutting down");
    Provisioning_Stop();
    NetworkManager_Stop();

    _NetworkTask_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Network_Task_Init(App_Context_t *p_AppContext)
{
    esp_err_t Error;
    App_Settings_WiFi_t WiFiSettings;
    App_Settings_Provisioning_t ProvisioningSettings;
    App_Settings_VISA_Server_t VISASettings;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_NetworkTask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing network task");

    _NetworkTask_State.AppContext = p_AppContext;

    /* Initialize NVS */
    Error = nvs_flash_init();
    if ((Error == ESP_ERR_NVS_NO_FREE_PAGES) || (Error == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        Error = nvs_flash_init();
    }

    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS flash: 0x%x!", Error);
        return Error;
    }

    /* Create event group */
    _NetworkTask_State.EventGroup = xEventGroupCreate();
    if (_NetworkTask_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        return ESP_ERR_NO_MEM;
    }

    if ((esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(SNTP_EVENTS, ESP_EVENT_ANY_ID, on_SNTP_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler, NULL) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to register event handler: %d!", Error);

        vEventGroupDelete(_NetworkTask_State.EventGroup);

        return Error;
    }

    /* Copy the required WiFi settings from settings to network config */
    SettingsManager_GetWiFi(&WiFiSettings);
    strncpy(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID, WiFiSettings.SSID,
            sizeof(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) - 1);
    strncpy(_NetworkTask_State.AppContext->STA_Config.Credentials.Password, WiFiSettings.Password,
            sizeof(_NetworkTask_State.AppContext->STA_Config.Credentials.Password) - 1);
    _NetworkTask_State.AppContext->STA_Config.MaxRetries = WiFiSettings.MaxRetries;
    _NetworkTask_State.AppContext->STA_Config.RetryInterval = WiFiSettings.RetryInterval;

    /* Copy the required VISA settings from settings to network config */
    SettingsManager_GetVISAServer(&VISASettings);
    _NetworkTask_State.AppContext->Server_Config.VISA_Server.Port = VISASettings.Port;

    /* Copy the required Provisioning settings from settings to network config */
    SettingsManager_GetProvisioning(&ProvisioningSettings);
    strncpy(_NetworkTask_State.AppContext->Prov_Config.Name,
            ProvisioningSettings.Name,
            sizeof(_NetworkTask_State.AppContext->Prov_Config.Name) - 1);
    _NetworkTask_State.AppContext->Prov_Config.Timeout = ProvisioningSettings.Timeout;

    Error = NetworkManager_Init(&_NetworkTask_State.AppContext->STA_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi manager: 0x%x!", Error);

        esp_event_handler_unregister(SNTP_EVENTS, ESP_EVENT_ANY_ID, on_SNTP_Event_Handler);
        esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
        esp_event_handler_unregister(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler);
        vEventGroupDelete(_NetworkTask_State.EventGroup);

        return Error;
    }

    Error = Provisioning_Init(&_NetworkTask_State.AppContext->Prov_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning: 0x%x!", Error);
        /* Continue anyway, provisioning is optional */
    }

    _NetworkTask_State.AppContext = p_AppContext;
    _NetworkTask_State.State = NETWORK_STATE_IDLE;
    _NetworkTask_State.isInitialized = true;

    ESP_LOGD(TAG, "Network Task initialized");

    return ESP_OK;
}

void Network_Task_Deinit(void)
{
    if (_NetworkTask_State.isInitialized == false) {
        return;
    }

    ESP_LOGD(TAG, "Deinitializing Network Task");

    Network_Task_Stop();
    Provisioning_Deinit();
    NetworkManager_Deinit();

    esp_event_handler_unregister(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler);
    esp_event_handler_unregister(SNTP_EVENTS, ESP_EVENT_ANY_ID, on_SNTP_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler);

    if (_NetworkTask_State.EventGroup != NULL) {
        vEventGroupDelete(_NetworkTask_State.EventGroup);
        _NetworkTask_State.EventGroup = NULL;
    }

    _NetworkTask_State.isInitialized = false;
}

esp_err_t Network_Task_Start(void)
{
    BaseType_t Ret;

    if (_NetworkTask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_NetworkTask_State.isRunning) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    _NetworkTask_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Network Task");

    Ret = xTaskCreatePinnedToCore(
              Task_Network,
              "Task_Network",
              CONFIG_NETWORK_TASK_STACKSIZE,
              NULL,
              CONFIG_NETWORK_TASK_PRIO,
              &_NetworkTask_State.TaskHandle,
              CONFIG_NETWORK_TASK_CORE
          );

    if (Ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task!");
        return ESP_ERR_NO_MEM;
    }

    _NetworkTask_State.StartTime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    return ESP_OK;
}

esp_err_t Network_Task_Stop(void)
{
    if (_NetworkTask_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Network Task");

    xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Network_Task_isRunning(void)
{
    return _NetworkTask_State.isRunning;
}