/*
 * application.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Application header file.
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
 */

#ifndef APPLICATION_H_
#define APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "Manager/managers.h"
#include "Manager/Devices/devicesTypes.h"

#include <sdkconfig.h>

ESP_EVENT_DECLARE_BASE(LEPTON_TASK_EVENTS);
ESP_EVENT_DECLARE_BASE(GUI_TASK_EVENTS);
ESP_EVENT_DECLARE_BASE(DEVICES_TASK_EVENTS);

/** @brief Lepton task event identifiers.
 */
enum {
    LEPTON_TASK_EVENT_CAMERA_READY,             /**< Lepton camera is ready.
                                                     Data is transmitted in a App_Lepton_Device_t structure. */
    LEPTON_TASK_EVENT_CAMERA_ERROR,             /**< Lepton camera error occurred. */
    LEPTON_TASK_EVENT_RESPONSE_FPA_AUX_TEMP,   /**< FPA and AUX temperatures are ready.
                                                     Data is transmitted in a App_Lepton_Temperatures_t structure. */
    LEPTON_TASK_EVENT_RESPONSE_UPTIME,          /**< Uptime data is ready.
                                                     Data is transmitted as a uint32_t representing uptime in milliseconds. */
    LEPTON_TASK_EVENT_RESPONSE_PIXEL_TEMPERATURE, /**< Pixel temperature data is ready.
                                                     Data is transmitted as a float. */
    LEPTON_TASK_EVENT_RESPONSE_SCENE_STATISTICS, /**< Scene statistics data is ready.
                                                     Data is transmitted in a App_Lepton_ROI_Result_t structure. */
};

/** @brief GUI task event identifiers.
 */
enum {
    GUI_TASK_EVENT_INIT_DONE,                   /**< GUI task initialization done. */
    GUI_TASK_EVENT_INIT_ERROR,                  /**< GUI task initialization error occurred. */
    GUI_TASK_EVENT_APP_STARTED,                 /**< Application has started. */
    GUI_TASK_EVENT_REQUEST_ROI,                 /**< Update the ROI rectangle on the GUI.
                                                     Data is transmitted in a Settings_ROI_t structure. */
    GUI_TASK_EVENT_REQUEST_FPA_AUX_TEMP,        /**< Request update of the FPA and AUX temperature. */
    GUI_TASK_EVENT_REQUEST_UPTIME,              /**< Request update of the uptime. */
    GUI_TASK_EVENT_REQUEST_PIXEL_TEMPERATURE,   /**< Request update of pixel temperature.
                                                     Data is transmitted in a App_GUI_Screenposition_t structure. */
    GUI_TASK_EVENT_REQUEST_SCENE_STATISTICS,    /**< Request update of scene statistics data. */
    GUI_TASK_EVENT_THERMAL_IMAGE_SAVED,         /**< Thermal image successfully saved to storage. */
    GUI_TASK_EVENT_THERMAL_IMAGE_SAVE_FAILED,   /**< Thermal image save operation failed.
                                                     Data is transmitted as an int representing the errno value. */
};

/** @brief Devices task event identifiers.
 */
enum {
    DEVICES_TASK_EVENT_RESPONSE_BATTERY,        /**< Battery status has been updated.
                                                     Data is transmitted in a App_Devices_Battery_t structure. */
    DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE,   /**< Temperature has been updated.
                                                     Data is transmitted in a App_Devices_Temperature_t structure. */
};

/** @brief Structure representing a screen position.
 */
typedef struct {
    int16_t x;                                  /**< X coordinate (0 to 159). */
    int16_t y;                                  /**< Y coordinate (0 to 119). */
    int32_t Width;                              /**< Width of the screen element where the position is related to. */
    int32_t Height;                             /**< Height of the screen element where the position is related to. */
} App_GUI_Screenposition_t;

/** @brief Structure representing battery information.
 */
typedef struct {
    int Voltage;                                /**< Battery voltage in millivolts. */
    uint8_t Percentage;                         /**< Battery percentage (0-100%). */
    bool Charging;                              /**< True if charging is in progress, false otherwise. */
} App_Devices_Battery_t;

/** @brief Structure representing a VL53L1X distance measurement.
 */
typedef struct {
    uint16_t Distance_mm;                       /**< Measured distance in millimeters. */
    bool IsValid;                               /**< True if the measurement status is VL53L1X_RANGE_VALID. */
} App_Devices_Distance_t;

/** @brief Structure representing a temperature measurement.
 */
typedef struct {
    float Temperature;                          /**< Measured temperature in degrees Celsius. */
} App_Devices_Temperature_t;

/** @brief Structure representing a ready frame from the Lepton camera.
 */
typedef struct {
    uint8_t *Buffer;                            /**< Pointer to the image buffer (Width * Height * Channels). */
    uint32_t Width;                             /**< Width of the frame in pixels. */
    uint32_t Height;                            /**< Height of the frame in pixels. */
    uint32_t Channels;                          /**< Number of color channels (e.g., 3 for RGB). */
    int16_t Min;                                /**< Minimum value in the frame. */
    int16_t Max;                                /**< Maximum value in the frame. */
} App_Lepton_FrameReady_t;

/** @brief Structure representing FPA and AUX temperature from the Lepton camera.
 */
typedef struct {
    float FPA;                                  /**< Focal Plane Array temperature in Degree Celsius. */
    float AUX;                                  /**< Auxiliary temperature in Degree Celsius. */
} App_Lepton_Temperatures_t;

/** @brief Structure representing the Lepton camera device status.
 */
typedef struct {
    char PartNumber[33];                        /**< Lepton device part number. */
    char SerialNumber[24];                      /**< Lepton device serial number. */
    struct {
        char GPP_Revision[24];                  /**< Lepton GPP software revision. */
        char DSP_Revision[24];                  /**< Lepton DSP software revision. */
    } SoftwareRevision;
} App_Lepton_Device_t;

/** @brief Structure representing the ROI results from the Lepton camera.
 */
typedef struct {
    float Max;                                  /**< Maximum value within the specified ROI. */
    float Min;                                  /**< Minimum value within the specified ROI. */
    union {
        float Average;                          /**< Average value within the specified ROI. */
        float Mean;                             /**< Mean value within the specified ROI. */
    };
} App_Lepton_ROI_Result_t;

/** @brief Application context aggregating shared resources.
 */
typedef struct {
    QueueHandle_t Lepton_FrameEventQueue;       /**< Queue for Lepton frame ready events. */
    SemaphoreHandle_t InputMutex;               /**< Protects InputState against concurrent access. */
    Devices_InputState_t InputState;            /**< Debounced displayboard input state, written by
                                                     Devices Task and read by the LVGL keypad indev. */
} App_Context_t;

#endif /* APPLICATION_H_ */