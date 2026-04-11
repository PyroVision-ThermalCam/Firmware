/*
 * remoteControl.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common remote control interface for VISA and HTTP/WebSocket servers.
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

#ifndef REMOTE_CONTROL_H_
#define REMOTE_CONTROL_H_

#include <esp_err.h>

#include <cJSON.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../networkTypes.h"
#include "Settings/settingsTypes.h"

/** @brief                  Get temperature from TMP117 sensor.
 *  @param p_Temperature    Pointer to store temperature value in Celsius
 *  @return                 ESP_OK on success
 *                          ESP_ERR_INVALID_ARG if p_Temperature is NULL
 *                          ESP_ERR_INVALID_STATE if sensor not initialized
 */
esp_err_t RemoteControl_GetTemperature(float *p_Temperature);

/** @brief              Get current system time.
 *  @param p_TimeStr    Pointer to buffer for time string (ISO 8601 format)
 *  @param MaxLen       Maximum length of buffer
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_TimeStr is NULL
 *                      ESP_ERR_INVALID_STATE if time not synchronized
 */
esp_err_t RemoteControl_GetTime(char *p_TimeStr, size_t MaxLen);

/** @brief              Set current system time.
 *  @param p_TimeStr    Time string in ISO 8601 format
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_TimeStr is NULL or invalid format
 */
esp_err_t RemoteControl_SetTime(const char *p_TimeStr);

/** @brief              Get battery voltage.
 *  @param p_Voltage    Pointer to store voltage value in millivolts
 *  @param p_SOC        Pointer to store SOC value in percent (0-100)
 *  @param p_Charging   Pointer to store charging state (true = charging in progress)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Voltage is NULL
 *                      ESP_ERR_NOT_SUPPORTED if battery monitoring not available
 */
esp_err_t RemoteControl_GetBatteryStatus(int *p_Voltage, uint8_t *p_SOC, bool *p_Charging);

/** @brief              Get OV5640 camera image.
 *  @param pp_Buffer    Pointer to store image buffer pointer (caller must free)
 *  @param p_Size       Pointer to store image size in bytes
 *  @param Format       Desired image format
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NO_MEM if allocation fails
 *                      ESP_ERR_NOT_FOUND if camera not available
 */
esp_err_t RemoteControl_GetOV5640Image(uint8_t **pp_Buffer, size_t *p_Size, Settings_Image_Format_t Format);

/** @brief              Get Lepton thermal camera image.
 *  @param pp_Buffer    Pointer to store image buffer pointer (caller must free)
 *  @param p_Size       Pointer to store image size in bytes
 *  @param Format       Desired image format
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 *                      ESP_ERR_NO_MEM if allocation fails
 *                      ESP_ERR_NOT_FOUND if camera not available
 */
esp_err_t RemoteControl_GetLeptonImage(uint8_t **pp_Buffer, size_t *p_Size, Settings_Image_Format_t Format);

/** @brief              Get Lepton camera emissivity.
 *  @param p_Emissivity Pointer to store emissivity value
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Emissivity is NULL
 */
esp_err_t RemoteControl_GetLeptonEmissivity(uint8_t *p_Emissivity);

/** @brief              Set Lepton camera emissivity.
 *  @param Emissivity   Emissivity value (0-100)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if value out of range
 */
esp_err_t RemoteControl_SetLeptonEmissivity(uint8_t Emissivity);

/** @brief              Get Lepton scene statistics.
 *  @param p_JSON       Pointer to cJSON object to populate (must be created)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_JSON is NULL
 *                      ESP_ERR_NOT_FOUND if no data available
 */
esp_err_t RemoteControl_GetLeptonSceneStats(cJSON *p_JSON);

/** @brief              Get Lepton ROI (Region of Interest).
 *  @param p_JSON       Pointer to cJSON object to populate with ROI data
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_JSON is NULL
 */
esp_err_t RemoteControl_GetLeptonROI(cJSON *p_JSON);

/** @brief              Set Lepton ROI (Region of Interest).
 *  @param p_JSON       Pointer to cJSON object containing ROI data
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_JSON is NULL or invalid
 */
esp_err_t RemoteControl_SetLeptonROI(const cJSON *p_JSON);

/** @brief              Get Lepton spotmeter data.
 *  @param p_JSON       Pointer to cJSON object to populate
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_JSON is NULL
 */
esp_err_t RemoteControl_GetLeptonSpotmeter(cJSON *p_JSON);

/** @brief              Update Lepton spotmeter data in server cache.
 *                      This function stores the latest spotmeter ROI results for use by
 *                      VISA and WebSocket/HTTP interfaces.
 *  @note               This function is thread-safe and can be called from any task.
 *  @param Min          Minimum temperature in Celsius
 *  @param Max          Maximum temperature in Celsius
 *  @param Mean         Mean temperature in Celsius
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_ROIResult is NULL
 */
esp_err_t RemoteControl_UpdateSpotmeter(float Min, float Max, float Mean);

/** @brief              Get flash configuration.
 *  @param p_Enabled    Pointer to store flash enabled state
 *  @param p_Power      Pointer to store flash power level (0-100)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if pointers are NULL
 */
esp_err_t RemoteControl_GetFlashConfig(bool *p_Enabled, uint8_t *p_Power);

/** @brief              Set flash power level.
 *  @param Power        Power level (0-100)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if value out of range
 */
esp_err_t RemoteControl_SetFlashPower(uint8_t Power);

/** @brief              Set flash state.
 *  @param Enabled      Flash enabled state
 *  @return             ESP_OK on success
 */
esp_err_t RemoteControl_SetFlashState(bool Enabled);

/** @brief              Get image format.
 *  @param p_Format     Pointer to store image format
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Format is NULL
 */
esp_err_t RemoteControl_GetImageFormat(Settings_Image_Format_t *p_Format);

/** @brief              Set image format.
 *  @param Format       Image format
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if format invalid
 */
esp_err_t RemoteControl_SetImageFormat(Settings_Image_Format_t Format);

/** @brief              Set status LED color and brightness.
 *  @param Color        LED color
 *  @param Brightness   Brightness level (0-255)
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if color invalid
 */
esp_err_t RemoteControl_SetStatusLED(Remote_LED_Color_t Color, uint8_t Brightness);

/** @brief              Check if SD card is available.
 *  @param p_Available  Pointer to store availability state
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Available is NULL
 */
esp_err_t RemoteControl_GetSDCardState(bool *p_Available);

/** @brief              Format active memory (internal flash or SD card).
 *  @return             ESP_OK on success
 *                      ESP_FAIL if format operation failed
 */
esp_err_t RemoteControl_FormatMemory(void);

/** @brief              Display message box on screen.
 *  @param p_Message    Message text to display
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Message is NULL
 */
esp_err_t RemoteControl_DisplayMessageBox(const char *p_Message);

/** @brief              Get lock state of input buttons and joystick.
 *  @param p_Locked     Pointer to store lock state
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Locked is NULL
 */
esp_err_t RemoteControl_GetLockState(bool *p_Locked);

/** @brief              Set lock state of input buttons and joystick.
 *  @param Locked       Lock state
 *  @return             ESP_OK on success
 */
esp_err_t RemoteControl_SetLockState(bool Locked);

#endif /* REMOTE_CONTROL_H_ */
