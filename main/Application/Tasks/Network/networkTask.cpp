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
#include "Application/Manager/Network/Server/RemoteControl/remoteControl.h"
#include "Application/Manager/Network/Server/ImageEncoder/imageEncoder.h"

#define NETWORK_TASK_STOP_REQUEST               BIT0
#define NETWORK_TASK_BROADCAST_FRAME            BIT1
#define NETWORK_TASK_PROV_SUCCESS               BIT2
#define NETWORK_TASK_WIFI_CONNECTED             BIT3
#define NETWORK_TASK_WIFI_DISCONNECTED          BIT4
#define NETWORK_TASK_WIFI_DATA_CHANGE           BIT9
#define NETWORK_TASK_OPEN_WIFI_REQUEST          BIT5
#define NETWORK_TASK_PROV_TIMEOUT               BIT6
#define NETWORK_TASK_SNTP_TIME_SYNCED           BIT8
#define NETWORK_TASK_SETTINGS_CHANGED           BIT7

typedef struct {
    bool isInitialized;
    bool isConnected;
    bool isRunning;
    bool ApplicationStarted;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint32_t StartTime;
    Network_State_t State;
    App_Lepton_ROI_Result_t ROIResult;
} Network_Task_State_t;

static Network_Task_State_t _Network_Task_State;

static const char *TAG = "Network-Task";

/** @brief                  Handler for the Lepton task events to receive updates when Lepton events are triggered (e.g., new frame ready, camera errors).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Lepton_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Lepton task event received: ID=%d", ID);

    switch (ID) {
    }
}

/** @brief                  GUI task event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI task event received: ID=%d", ID);

    switch (ID) {
        case GUI_TASK_EVENT_APP_STARTED: {
            ESP_LOGD(TAG, "Application started event received");

            _Network_Task_State.ApplicationStarted = true;

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Event handler for the SNTP events to receive updates when SNTP events are triggered (e.g., time synchronization, timezone changes).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_SNTP_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case SNTP_EVENT_SNTP_SYNCED: {
            ESP_LOGD(TAG, "SNTP time synchronized");

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_SNTP_TIME_SYNCED);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Network event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case NETWORK_EVENT_WIFI_CONNECTED: {
            ESP_LOGD(TAG, "WiFi connected");

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            ESP_LOGD(TAG, "WiFi disconnected");

            _Network_Task_State.State = NETWORK_STATE_DISCONNECTED;
            _Network_Task_State.isConnected = false;

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);

            break;
        }
        case NETWORK_EVENT_WIFI_GOT_IP: {
            _Network_Task_State.State = NETWORK_STATE_CONNECTED;
            _Network_Task_State.isConnected = true;

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            ESP_LOGD(TAG, "Provisioning success");

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);

            break;
        }
        case NETWORK_EVENT_PROV_FAILED: {
            ESP_LOGE(TAG, "Provisioning failed!");

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            ESP_LOGW(TAG, "Provisioning timeout - scheduling stop");

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_PROV_TIMEOUT);

            break;
        }
        case NETWORK_EVENT_OPEN_WIFI_REQUEST: {
            ESP_LOGD(TAG, "Open WiFi request received");

            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Settings event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Settings_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case SETTINGS_EVENT_WIFI_CHANGED: {
            SettingsManager_ChangeNotification_t Changed;

            memcpy(&Changed, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            if (Changed.ID == SETTINGS_ID_WIFI_SSID) {
                ESP_LOGD(TAG, "WiFi settings changed, ID: 0x%X", Changed.ID);

                xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_DATA_CHANGE);
            }

            break;
        }
        case SETTINGS_EVENT_SYSTEM_CHANGED: {
            xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_SETTINGS_CHANGED);

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
    Settings_WiFi_t WiFiSettings;

    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Network task started on core %d", xPortGetCoreID());

    while (_Network_Task_State.ApplicationStarted == false) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    SettingsManager_GetWiFi(&WiFiSettings);

    ESP_LOGD(TAG, "Autoconnect is %s", (WiFiSettings.AutoConnect) ? "enabled" : "disabled");

    /* If autoconnect is disabled, wait for explicit WiFi open request in main loop */
    bool WaitingForWiFiRequest = (WiFiSettings.AutoConnect == false);

    if (WaitingForWiFiRequest == false) {
        SettingsManager_GetWiFi(&WiFiSettings);

        /* Check if WiFi credentials are available */
        if (strlen(WiFiSettings.SSID) == 0) {
            ESP_LOGW(TAG, "No credentials found, starting provisioning");

            Provisioning_Start();

            _Network_Task_State.State = NETWORK_STATE_PROVISIONING;
        } else {
            ESP_LOGD(TAG, "Credentials found, connecting to WiFi");

            NetworkManager_StartSTA();
        }
    } else {
        ESP_LOGD(TAG, "Waiting for WiFi open request...");
        _Network_Task_State.State = NETWORK_STATE_IDLE;
    }

    while (_Network_Task_State.isRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_Network_Task_State.EventGroup);
        if (EventBits & NETWORK_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _Network_Task_State.isRunning = false;

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & NETWORK_TASK_WIFI_CONNECTED) {
            /* Notify Time Manager that network is available */
            TimeManager_OnNetworkConnected();

            if (NetworkManager_StartServer() == ESP_OK) {
                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_STARTED, NULL, 0, pdMS_TO_TICKS(100));
            } else {
                ESP_LOGE(TAG, "Failed to start HTTP/WebSocket server");

                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_ERROR, NULL, 0, pdMS_TO_TICKS(100));
            }

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);
        }

        if (EventBits & NETWORK_TASK_WIFI_DISCONNECTED) {
            ESP_LOGD(TAG, "Handling WiFi disconnection");

            SettingsManager_GetWiFi(&WiFiSettings);

            /* Notify Time Manager that network is unavailable */
            TimeManager_OnNetworkDisconnected();

            /* Only start provisioning if not already active and we have no credentials */
            if ((Provisioning_IsActive() == false) && (strlen(WiFiSettings.SSID) == 0)) {
                Provisioning_Start();
            }

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);
        }

        if (EventBits & NETWORK_TASK_PROV_SUCCESS) {
            ESP_LOGD(TAG, "Provisioning success - stopping provisioning and connecting to WiFi");

            /* Provisioning_Stop() calls esp_wifi_stop() which can block for several seconds.
             * NetworkManager_StartSTA() also performs blocking WiFi initialization.
             * Unregister from WDT for the duration to prevent false WDT triggers. */
            esp_task_wdt_delete(NULL);

            /* Stop provisioning (HTTP server on port 80 and DNS) */
            Provisioning_Stop();

            ESP_LOGD(TAG, "Provisioning stopped, starting WiFi STA connection");

            /* Connect to WiFi with the new credentials */
            NetworkManager_StartSTA();

            ESP_LOGD(TAG, "WiFi STA connection initiated");

            /* Re-register with WDT now that the long blocking operations are complete */
            esp_task_wdt_add(NULL);
            esp_task_wdt_reset();

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);
        }

        if (EventBits & NETWORK_TASK_PROV_TIMEOUT) {
            ESP_LOGD(TAG, "Handling provisioning timeout");

            /* Provisioning_Stop() calls esp_wifi_stop() which can block for several seconds.
             * Unregister from WDT for the duration to prevent false WDT triggers. */
            esp_task_wdt_delete(NULL);

            Provisioning_Stop();

            /* Re-register with WDT now that the long blocking operation is complete */
            esp_task_wdt_add(NULL);
            esp_task_wdt_reset();

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_PROV_TIMEOUT);
        }

        if (EventBits & NETWORK_TASK_OPEN_WIFI_REQUEST) {
            ESP_LOGD(TAG, "Handling WiFi open request");

            /* Start WiFi connection if we were waiting */
            if (WaitingForWiFiRequest) {
                SettingsManager_GetWiFi(&WiFiSettings);

                if ((Provisioning_isProvisioned() == false) && (strlen(WiFiSettings.SSID) == 0)) {
                    ESP_LOGW(TAG, "No credentials found, starting provisioning");

                    Provisioning_Start();

                    _Network_Task_State.State = NETWORK_STATE_PROVISIONING;
                } else {
                    NetworkManager_StartSTA();
                }

                WaitingForWiFiRequest = false;
            }

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);
        } else if (EventBits & NETWORK_TASK_WIFI_DATA_CHANGE) {
            ESP_LOGD(TAG, "Handling WiFi data change");

            /* If we're currently connected, restart WiFi to apply new settings */
            if (_Network_Task_State.isConnected) {
                NetworkManager_Stop();
                NetworkManager_StartSTA();
            }

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_WIFI_DATA_CHANGE);
        }

        if (EventBits & NETWORK_TASK_SNTP_TIME_SYNCED) {
            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_SNTP_TIME_SYNCED);
        }

        if (EventBits & NETWORK_TASK_SETTINGS_CHANGED) {
            Settings_System_t SystemSettings;

            SettingsManager_GetSystem(&SystemSettings);
            ImageEncoder_SetQuality(SystemSettings.JpegQuality);

            ESP_LOGD(TAG, "System settings applied - JPEG quality: 0x%X", SystemSettings.JpegQuality);

            xEventGroupClearBits(_Network_Task_State.EventGroup, NETWORK_TASK_SETTINGS_CHANGED);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGD(TAG, "Network task shutting down");
    Provisioning_Stop();
    NetworkManager_Stop();

    _Network_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Network_Task_Init(App_Context_t *p_AppContext)
{
    esp_err_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Network_Task_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing network task");

    Error = nvs_flash_init();
    if ((Error == ESP_ERR_NVS_NO_FREE_PAGES) || (Error == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        Error = nvs_flash_init();
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS flash: 0x%x!", Error);

        return Error;
    }

    _Network_Task_State.EventGroup = xEventGroupCreate();
    if (_Network_Task_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");

        return ESP_ERR_NO_MEM;
    }

    if ((esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, on_GUI_Task_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(SNTP_EVENTS, SNTP_EVENT_SNTP_SYNCED, on_SNTP_Event_Handler, NULL) != ESP_OK) ||
        (esp_event_handler_register(SETTINGS_EVENTS, ESP_EVENT_ANY_ID, on_Settings_Event_Handler, NULL) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to register event handler: 0x%X!", Error);

        vEventGroupDelete(_Network_Task_State.EventGroup);

        return Error;
    }

    Error = NetworkManager_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi manager: 0x%x!", Error);

        esp_event_handler_unregister(SNTP_EVENTS, SNTP_EVENT_SNTP_SYNCED, on_SNTP_Event_Handler);
        esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
        esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, on_GUI_Task_Event_Handler);
        esp_event_handler_unregister(SETTINGS_EVENTS, ESP_EVENT_ANY_ID, on_Settings_Event_Handler);
        vEventGroupDelete(_Network_Task_State.EventGroup);

        return Error;
    }

    Error = Provisioning_Init();
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning: 0x%x!", Error);
        /* Continue anyway, provisioning is optional */
    }

    _Network_Task_State.State = NETWORK_STATE_IDLE;
    _Network_Task_State.isInitialized = true;

    ESP_LOGD(TAG, "Network Task initialized");

    return ESP_OK;
}

void Network_Task_Deinit(void)
{
    if (_Network_Task_State.isInitialized == false) {
        return;
    }

    ESP_LOGD(TAG, "Deinitializing Network Task");

    Network_Task_Stop();
    Provisioning_Deinit();
    NetworkManager_Deinit();

    esp_event_handler_unregister(SNTP_EVENTS, SNTP_EVENT_SNTP_SYNCED, on_SNTP_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, ESP_EVENT_ANY_ID, on_Settings_Event_Handler);

    if (_Network_Task_State.EventGroup != NULL) {
        vEventGroupDelete(_Network_Task_State.EventGroup);
        _Network_Task_State.EventGroup = NULL;
    }

    _Network_Task_State.isInitialized = false;
}

esp_err_t Network_Task_Start(void)
{
    BaseType_t Error;

    if (_Network_Task_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_Network_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    _Network_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting Network Task");

    Error = xTaskCreatePinnedToCore(Task_Network, "Task_Network", CONFIG_NETWORK_TASK_STACKSIZE, NULL,
                                    CONFIG_NETWORK_TASK_PRIO, &_Network_Task_State.TaskHandle, CONFIG_NETWORK_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Network Task: 0x%X!", Error);

        return ESP_ERR_NO_MEM;
    }

    _Network_Task_State.StartTime = xTaskGetTickCount() / configTICK_RATE_HZ;

    return ESP_OK;
}

esp_err_t Network_Task_Stop(void)
{
    if (_Network_Task_State.isRunning == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Network Task");

    xEventGroupSetBits(_Network_Task_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool Network_Task_IsRunning(void)
{
    return _Network_Task_State.isRunning;
}