/*
 * settingsManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings Manager definition.
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

#ifndef SETTINGS_MANAGER_H_
#define SETTINGS_MANAGER_H_

#include <esp_err.h>
#include <stdbool.h>

#include "settingsTypes.h"

/** @brief          Initialize the Settings Manager and load settings from NVS.
 *                  Opens NVS namespace, loads stored settings into RAM, or loads defaults
 *                  if no settings exist. Creates event handlers for settings changes.
 *  @note           Must be called after NVS flash initialization.
 *                  Default settings loaded from JSON or hardcoded fallback.
 *                  Call this before any other SettingsManager functions.
 *  @warning        Not thread-safe during initialization.
 *  @return         ESP_OK on success
 *                  ESP_ERR_NVS_NOT_FOUND if NVS namespace doesn't exist (first boot)
 *                  ESP_ERR_NO_MEM if memory allocation fails
 *                  ESP_ERR_INVALID_STATE if already initialized
 *                  ESP_FAIL if NVS initialization fails
 */
esp_err_t SettingsManager_Init(void);

/** @brief          Deinitialize the Settings Manager.
 *                  Closes NVS handle and frees all resources. Unsaved settings in RAM
 *                  are lost.
 *  @note           Call SettingsManager_Save() first to persist changes.
 *  @warning        All unsaved settings changes are lost permanently.
 *  @return         ESP_OK on success
 *                  ESP_FAIL if NVS close fails
 */
esp_err_t SettingsManager_Deinit(void);

/** @brief              Load all settings from NVS into RAM.
 *                      Reloads settings from NVS, overwriting any unsaved changes in RAM.
 *                      Use this to discard uncommitted changes.
 *  @note               This overwrites all unsaved settings in RAM.
 *                      Version mismatch triggers default settings reload.
 *  @warning            All uncommitted changes are lost!
 *  @param p_Settings   Pointer to settings structure to populate
 *  @return             ESP_OK on success
 *                      ESP_ERR_INVALID_ARG if p_Settings is NULL
 *                      ESP_ERR_NVS_INVALID_STATE if config_valid flag is missing or false
 *                      ESP_ERR_NVS_NOT_FOUND if no settings exist in NVS
 *                      ESP_ERR_INVALID_VERSION if version mismatch
 *                      ESP_ERR_INVALID_SIZE if size mismatch (corrupted)
 *                      ESP_FAIL on other NVS errors
 */
esp_err_t SettingsManager_LoadFromNVS(Settings_t *p_Settings);

/** @brief          Save all RAM settings to NVS.
 *                  Writes current settings from RAM to non-volatile storage. Changes
 *                  become permanent and survive power cycles.
 *  @note           Call this after any Update functions to persist changes.
 *                  NVS has limited write cycles (~100k) - avoid excessive saves.
 *                  Posts SETTINGS_EVENT_SAVED event on success.
 *  @return         ESP_OK on success
 *                  ESP_ERR_INVALID_STATE if not initialized
 *                  ESP_ERR_NVS_NOT_ENOUGH_SPACE if NVS is full
 *                  ESP_FAIL if NVS write fails
 */
esp_err_t SettingsManager_Save(void);

/** @brief              Get the device information from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Info structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetInfo(Settings_Info_t *p_Settings);

/** @brief              Get the Lepton settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to System settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetLepton(Settings_Lepton_t *p_Settings);

/** @brief                  Update Lepton settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_LEPTON_CHANGED event.
 *  @param p_Settings       Pointer to Lepton settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateLepton(Settings_Lepton_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the WiFi settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to WiFi settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetWiFi(Settings_WiFi_t *p_Settings);

/** @brief                  Update WiFi settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_WIFI_CHANGED event.
 *  @param p_Settings       Pointer to WiFi settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateWiFi(Settings_WiFi_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the Provisioning settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Provisioning settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetProvisioning(Settings_Provisioning_t *p_Settings);

/** @brief                  Update Provisioning settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_PROVISIONING_CHANGED event.
 *  @param p_Settings       Pointer to Provisioning settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateProvisioning(Settings_Provisioning_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the Display settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Display settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetDisplay(Settings_Display_t *p_Settings);

/** @brief                  Update Display settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_DISPLAY_CHANGED event.
 *  @param p_Settings       Pointer to Display settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateDisplay(Settings_Display_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the HTTP Server settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to HTTP Server settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetHTTPServer(Settings_HTTP_Server_t *p_Settings);

/** @brief                  Update HTTP Server settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_HTTP_SERVER_CHANGED event.
 *  @param p_Settings       Pointer to HTTP Server settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateHTTPServer(Settings_HTTP_Server_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the VISA Server settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to VISA Server settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetVISAServer(Settings_VISA_Server_t *p_Settings);

/** @brief                  Update VISA Server settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_VISA_SERVER_CHANGED event.
 *  @param p_Settings       Pointer to VISA Server settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateVISAServer(Settings_VISA_Server_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the System settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to System settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetSystem(Settings_System_t *p_Settings);

/** @brief                  Update System settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_SYSTEM_CHANGED event.
 *  @param p_Settings       Pointer to System settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateSystem(Settings_System_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the LED Flash settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to LED Flash settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetLEDFlash(Settings_LED_Flash_t *p_Settings);
    
/** @brief                  Update LED Flash settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_LED_FLASH_CHANGED event.
 *  @param p_Settings       Pointer to LED Flash settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateLEDFlash(Settings_LED_Flash_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief              Get the USB settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to USB settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetUSB(Settings_USB_t *p_Settings);

/** @brief                  Update USB settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_USB_CHANGED event.
 *  @param p_Settings       Pointer to USB settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateUSB(Settings_USB_t *p_Settings,
                                       SettingsManager_ChangeNotification_t *p_ChangedSetting = NULL);

/** @brief  Reset all settings to factory defaults.
 *          Erases NVS partition and reloads defaults.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_ResetToDefaults(void);

#endif /* SETTINGS_MANAGER_H_ */
