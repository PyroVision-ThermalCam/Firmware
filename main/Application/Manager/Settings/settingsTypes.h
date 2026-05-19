/*
 * settingsTypes.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the Settings Manager component.
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

#include <esp_err.h>
#include <esp_event.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../ImageEncoder/imageEncoderTypes.h"

#define SETTINGS_ERR_BASE                       0x4000

/** @defgroup SETTINGS_ERRORS Settings Manager Error Codes
 *  @brief Error codes returned by SettingsManager functions (base: @c SETTINGS_ERR_BASE = 0x4000).
 *  @{
 */

/** @brief SettingsManager_Init() has not been called yet. */
#define SETTINGS_ERR_NOT_INITIALIZED            (SETTINGS_ERR_BASE + 0x01)

/** @brief SettingsManager_Init() called more than once (already initialised). */
#define SETTINGS_ERR_ALREADY_INITIALIZED        (SETTINGS_ERR_BASE + 0x02)

/** @brief NVS namespace not found — first boot or NVS partition erased. */
#define SETTINGS_ERR_NVS_NOT_FOUND              (SETTINGS_ERR_BASE + 0x03)

/** @brief NVS partition is full — settings cannot be saved. */
#define SETTINGS_ERR_NVS_FULL                   (SETTINGS_ERR_BASE + 0x04)

/** @brief Settings version mismatch — firmware updated with an incompatible settings layout. */
#define SETTINGS_ERR_VERSION_MISMATCH           (SETTINGS_ERR_BASE + 0x05)

/** @brief Settings size mismatch — stored structure size differs from compiled size (corrupted or layout changed). */
#define SETTINGS_ERR_SIZE_MISMATCH              (SETTINGS_ERR_BASE + 0x06)

/** @brief NVS write operation failed during SettingsManager_Save(). */
#define SETTINGS_ERR_NVS_WRITE                  (SETTINGS_ERR_BASE + 0x07)

/** @brief NVS read operation failed during SettingsManager_LoadFromNVS(). */
#define SETTINGS_ERR_NVS_READ                   (SETTINGS_ERR_BASE + 0x08)

/** @brief Default settings JSON file not found or failed to parse. */
#define SETTINGS_ERR_DEFAULT_JSON               (SETTINGS_ERR_BASE + 0x09)

/** @} */

/** @brief  Version number for the NVS based settings structure.
 *          NOTE: Migration isnt suppored yet!
 */
#define SETTINGS_VERSION                        1

/** @brief Settings Manager events base.
 */
ESP_EVENT_DECLARE_BASE(SETTINGS_EVENTS);

/** @brief Settings Manager event identifiers.
 */
enum {
    SETTINGS_EVENT_LOADED,                      /**< Settings loaded from NVS. */
    SETTINGS_EVENT_SAVED,                       /**< Settings saved to NVS. */
    SETTINGS_EVENT_LEPTON_CHANGED,              /**< Lepton settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_WIFI_CHANGED,                /**< WiFi settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_PROVISIONING_CHANGED,        /**< Provisioning settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_DISPLAY_CHANGED,             /**< Display settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_HTTP_SERVER_CHANGED,         /**< HTTP server settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_VISA_SERVER_CHANGED,         /**< VISA server settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_SYSTEM_CHANGED,              /**< System settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_LED_FLASH_CHANGED,           /**< LED flash settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_USB_CHANGED,                 /**< USB settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_CALIBRATION_CHANGED,         /**< Calibration settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_IMAGE_CHANGED,               /**< Image settings changed.
                                                     Data contains SettingsManager_ChangeNotification_t. */
    SETTINGS_EVENT_REQUEST_GET,                 /**< Request to get current settings. */
    SETTINGS_EVENT_REQUEST_SAVE,                /**< Request to save settings to NVS. */
    SETTINGS_EVENT_REQUEST_RESET,               /**< Request to reset settings to factory defaults. */
};

/** @brief Settings change identifiers.
 */
enum {
    SETTINGS_ID_LEPTON_EMISSIVITY,              /**< Emissivity setting changed.
                                                     Data contains uint8_t with new emissivity value. */
    SETTINGS_ID_LEPTON_PALETTE,                 /**< Palette setting changed.
                                                     Data contains uint8_t with new Lepton_Palette_t value. */
    SETTINGS_ID_DISPLAY_BRIGHTNESS,             /**< Display brightness setting changed.
                                                     Data contains uint8_t with new brightness value. */
    SETTINGS_ID_DISPLAY_TIMEOUT,                /**< Display timeout setting changed.
                                                     Data contains uint8_t with new timeout value. */
    SETTINGS_ID_IMAGE_FORMAT,                   /**< Image format setting changed.
                                                     Data contains Settings_Image_Format_t. */
    SETTINGS_ID_WIFI_SSID,                      /**< WiFi settings changed.
                                                     Data must be set to 0. */
    SETTINGS_ID_SNTP_TIMEZONE,                  /**< SNTP timezone changed.
                                                     Data must be set to 0. */
    SETTINGS_ID_LED_FLASH_ENABLE,              /**< LED flash enable setting changed.
                                                     Data contains bool with new enabled state. */
    SETTINGS_ID_LED_FLASH_POWER,                /**< LED flash power setting changed.
                                                     Data contains uint8_t with new power value. */
    SETTINGS_ID_CALIBRATION_ROOM_TEMP,          /**< Room temperature calibration changed.
                                                     Data contains int16_t with new temperature in °C. */
    SETTINGS_ID_CALIBRATION_INTERVAL,           /**< Calibration interval changed.
                                                     Data contains uint32_t with new interval in seconds. */
};

/** @brief GUI ROI types.
 */
typedef enum {
    ROI_TYPE_SPOTMETER,                         /**< Spotmeter ROI. */
    ROI_TYPE_SCENE,                             /**< Scene statistics ROI. */
    ROI_TYPE_AGC,                               /**< AGC ROI. */
    ROI_TYPE_VIDEO_FOCUS,                       /**< Video focus ROI. */
} Settings_ROI_Type_t;

/** @brief Structure to hold the modified settings value.
 */
typedef struct {
    uint32_t ID;                                /**< Identifier for the changed setting. */
    uint32_t Value;                             /**< New value of the changed setting (can be cast to the appropriate type based on ID). */
} SettingsManager_ChangeNotification_t;

/** @brief Emissivity setting definition.
 */
typedef struct {
    float Value;                                /**< Emissivity value (0-100). */
    char Description[32];                       /**< Description of the emissivity setting. */
} Settings_Emissivity_t;

/** @brief Region of Interest (ROI) rectangle definition (based on Display coordinates).
 */
typedef struct {
    Settings_ROI_Type_t Type;                   /**< ROI type (e.g., spotmeter). */
    int16_t x;                                  /**< X coordinate of the top-left corner. */
    int16_t y;                                  /**< Y coordinate of the top-left corner. */
    int16_t w;                                  /**< Width of the ROI. */
    int16_t h;                                  /**< Height of the ROI. */
} Settings_ROI_t;

/** @brief  Device informations.
 *          NOTE: This structure is not covered by the settings version number because it is not stored in the NVS.
 */
typedef struct {
    char FirmwareVersion[16];                   /**< Firmware version string, null-terminated. */
    char Manufacturer[32];                      /**< Manufacturer string, null-terminated. */
    char Name[32];                              /**< Device name string, null-terminated. */
    char Serial[16];                            /**< Device serial number string, null-terminated. */
    char SDK[16];                               /**< Firmware SDK version string, null-terminated. */
    char Commit[32];                            /**< Firmware commit hash string, null-terminated. */
    esp_app_desc_t Bootloader;                  /**< Bootloader information. */
} Settings_Info_t;

/** @brief  Lepton camera settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    Settings_ROI_t ROI[4];                          /**< Camera ROIs in Lepton coordinates. */
    Settings_Emissivity_t EmissivityPresets[128];   /**< Array of emissivity presets. */
    size_t EmissivityPresetsCount;                  /**< Number of emissivity presets. */
    uint8_t CurrentEmissivity;                      /**< Currently selected emissivity value in the range from 0 to 100. */
    uint8_t Palette;                                /**< Active color palette index (Lepton_Palette_t). */
} __attribute__((packed)) Settings_Lepton_t;

/** @brief  WiFi settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    char SSID[33];                              /**< WiFi SSID. */
    char Password[65];                          /**< WiFi password. */
    bool AutoConnect;                           /**< Automatically connect to known WiFi networks. */
    uint8_t MaxRetries;                         /**< Maximum number of connection retries. */
    uint16_t RetryInterval;                     /**< Interval between connection retries in milliseconds. */
} __attribute__((packed)) Settings_WiFi_t;

/** @brief  Provisioning settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    char Name[32];                              /**< Device name for provisioning. */
    uint32_t Timeout;                           /**< Provisioning timeout in seconds. */
} __attribute__((packed)) Settings_Provisioning_t;

/** @brief  Display settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    uint8_t Brightness;                         /**< Display brightness (0-100%). */
    uint16_t Timeout;                           /**< Screen timeout in seconds (0 = never). */
} __attribute__((packed)) Settings_Display_t;

/** @brief  HTTP server settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    uint16_t Port;                              /**< HTTP server port. */
    uint16_t WSPingIntervalSec;                 /**< WebSocket ping interval in seconds. */
    uint8_t MaxClients;                         /**< Maximum number of simultaneous clients. */
    bool UseCORS;                               /**< Whether to enable CORS headers. */
    char APIKey[64];                            /**< API key for authentication (null-terminated). */
} __attribute__((packed)) Settings_HTTP_Server_t;

/** @brief  VISA server settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    uint16_t Port;                              /**< VISA server port. */
    uint16_t Timeout;                           /**< VISA server socket timeout in milliseconds. */
} __attribute__((packed)) Settings_VISA_Server_t;

/** @brief  System settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    bool SDCard_AutoMount;                      /**< Automatically mount SD card. */
    char Timezone[32];                          /**< Timezone string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3"). */
    char NTPServer[32];                         /**< NTP server address. */
    char DeviceName[32];                        /**< Device name. */
    uint8_t Reserved[103];                       /**< Reserved for future use. */
} __attribute__((packed)) Settings_System_t;

typedef struct {
    ImageEncoder_Format_t Format;               /**< Image format for captures. */
    uint8_t JpegQuality;                        /**< JPEG compression quality (1-100). */
} __attribute__((packed)) Settings_Image_t;

/** @brief  LED flash settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    bool Enable;                                /**< Whether to enable LED flash when capturing images. */
    uint8_t Power;                              /**< LED flash power (0-100%). */
} __attribute__((packed)) Settings_LED_Flash_t;

/** @brief  USB settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    bool MSC_Enabled;                           /**< Enable USB Mass Storage Class mode. */
    bool UVC_Enabled;                           /**< Enable USB Video Class (UVC) mode. */
    bool CDC_Enabled;                           /**< Enable USB Communication Device Class (CDC-ACM) mode. */
    uint8_t Reserved[5];                        /**< Reserved for future use. */
} __attribute__((packed)) Settings_USB_t;

/** @brief  Calibration settings.
 *          NOTE: This structure is covered by the settings version number because it is stored in the NVS.
 */
typedef struct {
    int16_t RoomTemperature;                    /**< User-entered room temperature in °C at calibration time. */
    uint32_t Interval;                          /**< Calibration interval in seconds. */
    float SensorAtCalibration;                  /**< On-board sensor reading in °C at the time of calibration.
                                                     Used to compute the persistent offset:
                                                     offset = RoomTemperature − SensorAtCalibration
                                                     estimated_ambient = sensor_current + offset */
    uint8_t Reserved[2];                        /**< Reserved for future use. */
} __attribute__((packed)) Settings_Calibration_t;

/** @brief Complete application settings structure.
 */
typedef struct {
    uint32_t Version;                           /**< Settings version number. */
    Settings_Lepton_t Lepton;                   /**< Lepton camera settings. */
    Settings_WiFi_t WiFi;                       /**< WiFi settings. */
    Settings_Provisioning_t Provisioning;       /**< Provisioning settings. */
    Settings_Display_t Display;                 /**< Display settings. */
    Settings_HTTP_Server_t HTTPServer;          /**< HTTP server settings. */
    Settings_VISA_Server_t VISAServer;          /**< VISA server settings. */
    Settings_System_t System;                   /**< System settings. */
    Settings_LED_Flash_t LEDFlash;              /**< LED flash settings. */
    Settings_USB_t USB;                         /**< USB settings. */
    Settings_Calibration_t Calibration;         /**< Calibration settings. */
    Settings_Image_t Image;                     /**< Image settings. */
} Settings_t;

#endif /* SETTINGS_TYPES_H_ */
