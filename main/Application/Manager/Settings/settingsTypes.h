/*
 * settings_types.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings Manager event types and definitions.
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

#ifndef SETTINGS_TYPES_H_
#define SETTINGS_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <esp_event.h>

/** @brief Settings Manager events base.
 */
ESP_EVENT_DECLARE_BASE(SETTINGS_EVENTS);

/** @brief Settings event identifiers.
 */
enum {
    SETTINGS_EVENT_LOADED,                      /**< Settings loaded from NVS. */
    SETTINGS_EVENT_SAVED,                       /**< Settings saved to NVS. */
    SETTINGS_EVENT_LEPTON_CHANGED,              /**< Lepton settings changed.
                                                     Data contains App_Settings_Lepton_t. */
    SETTINGS_EVENT_WIFI_CHANGED,                /**< WiFi settings changed.
                                                     Data contains App_Settings_WiFi_t. */
    SETTINGS_EVENT_PROVISIONING_CHANGED,        /**< Provisioning settings changed.
                                                     Data contains App_Settings_Provisioning_t. */
    SETTINGS_EVENT_DISPLAY_CHANGED,             /**< Display settings changed.
                                                     Data contains App_Settings_Display_t. */
    SETTINGS_EVENT_HTTP_SERVER_CHANGED,        /**< HTTP server settings changed.
                                                     Data contains App_Settings_HTTP_Server_t. */
    SETTINGS_EVENT_VISA_SERVER_CHANGED,         /**< VISA server settings changed.
                                                     Data contains App_Settings_VISA_Server_t. */
    SETTINGS_EVENT_SYSTEM_CHANGED,              /**< System settings changed.
                                                     Data contains App_Settings_System_t. */
    SETTINGS_EVENT_REQUEST_GET,                 /**< Request to get current settings. */
    SETTINGS_EVENT_REQUEST_SAVE,                /**< Request to save settings to NVS. */
    SETTINGS_EVENT_REQUEST_RESET,               /**< Request to reset settings to factory defaults. */
};

/** @brief GUI ROI types.
 */
typedef enum {
    ROI_TYPE_SPOTMETER,                         /**< Spotmeter ROI. */
    ROI_TYPE_SCENE,                             /**< Scene statistics ROI. */
    ROI_TYPE_AGC,                               /**< AGC ROI. */
    ROI_TYPE_VIDEO_FOCUS,                       /**< Video focus ROI. */
} App_Settings_ROI_Type_t;

/** @brief Emissivity setting definition.
 */
typedef struct {
    float Value;                                /**< Emissivity value (0-100). */
    char Description[32];                       /**< Description of the emissivity setting. */
} App_Settings_Emissivity_t;

/** @brief Region of Interest (ROI) rectangle definition (based on Display coordinates).
 */
typedef struct {
    App_Settings_ROI_Type_t Type;               /**< ROI type (e.g., spotmeter). */
    uint16_t x;                                 /**< X coordinate of the top-left corner. */
    uint16_t y;                                 /**< Y coordinate of the top-left corner. */
    uint16_t w;                                 /**< Width of the ROI. */
    uint16_t h;                                 /**< Height of the ROI. */
} App_Settings_ROI_t;

/** @brief Lepton camera settings.
 */
typedef struct {
    App_Settings_ROI_t ROI[4];                  /**< Camera ROIs. */
    App_Settings_Emissivity_t EmissivityPresets[128];   /**< Array of emissivity presets. */
    size_t EmissivityCount;                     /**< Number of emissivity presets. */
} __attribute__((packed)) App_Settings_Lepton_t;

/** @brief WiFi settings.
 */
typedef struct {
    char SSID[33];                              /**< WiFi SSID. */
    char Password[65];                          /**< WiFi password. */
    bool AutoConnect;                           /**< Automatically connect to known WiFi networks. */
    uint8_t MaxRetries;                         /**< Maximum number of connection retries. */
    uint16_t RetryInterval;                     /**< Interval between connection retries in milliseconds. */
} __attribute__((packed)) App_Settings_WiFi_t;

/** @brief Provisioning settings.
 */
typedef struct {
    char Name[32];                              /**< Device name for provisioning. */
    uint32_t Timeout;                           /**< Provisioning timeout in seconds. */
} __attribute__((packed)) App_Settings_Provisioning_t;

/** @brief Device informations.
 */
typedef struct {
    char FirmwareVersion[16];                   /**< Firmware version string. */
    char Manufacturer[16];                      /**< Manufacturer string. */
    uint16_t Serial;                            /**< Device serial number. */
} __attribute__((packed)) App_Settings_Info_t;

/** @brief Display settings.
 */
typedef struct {
    uint8_t Brightness;                         /**< Display brightness (0-100%). */
    uint16_t Timeout;                           /**< Screen timeout in seconds (0=never). */
} __attribute__((packed)) App_Settings_Display_t;

/** @brief HTTP server settings.
 */
typedef struct {
    uint16_t Port;                              /**< HTTP server port. */
    uint16_t WSPingIntervalSec;                 /**< WebSocket ping interval in seconds. */
    uint8_t MaxClients;                         /**< Maximum number of simultaneous clients. */
} __attribute__((packed)) App_Settings_HTTP_Server_t;

/** @brief VISA server settings.
 */
typedef struct {
    uint16_t Port;                              /**< VISA server port. */
} __attribute__((packed)) App_Settings_VISA_Server_t;

/** @brief System settings.
 */
typedef struct {
    bool SDCard_AutoMount;                      /**< Automatically mount SD card. */
    char Timezone[32];                          /**< Timezone string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3"). */
    char NTPServer[32];                         /**< NTP server address. */
    char DeviceName[32];                        /**< Device name. */
    uint8_t Reserved[100];                      /**< Reserved for future use. */
} __attribute__((packed)) App_Settings_System_t;

/** @brief Complete application settings structure.
 */
typedef struct {
    App_Settings_Info_t Info;                   /**< General device information. */
    App_Settings_Lepton_t Lepton;               /**< Lepton camera settings. */
    App_Settings_WiFi_t WiFi;                   /**< WiFi settings. */
    App_Settings_Provisioning_t Provisioning;   /**< Provisioning settings. */
    App_Settings_Display_t Display;             /**< Display settings. */
    App_Settings_HTTP_Server_t HTTPServer;      /**< HTTP server settings. */
    App_Settings_VISA_Server_t VISAServer;      /**< VISA server settings. */
    App_Settings_System_t System;               /**< System settings. */
} __attribute__((packed)) App_Settings_t;

#endif /* SETTINGS_TYPES_H_ */
