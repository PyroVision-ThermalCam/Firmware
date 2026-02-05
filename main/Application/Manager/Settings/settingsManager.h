/*
 * settingsManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Persistent settings management using NVS storage.
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

/** @brief  Initialize the Settings Manager and load all settings from NVS into the Settings Manager RAM and into the provided structure.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_Init(void);

/** @brief  Deinitialize the Settings Manager.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_Deinit(void);

/** @brief              Load all settings from NVS into the Settings Manager RAM and into the provided structure. This function overwrites all unsaved settings in RAM.
 *  @param p_Settings   Pointer to settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no settings exist
 */
esp_err_t SettingsManager_Load(App_Settings_t *p_Settings);

/** @brief  Save all RAM settings to NVS.
 *  @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_Save(void);

/** @brief              Get the device information from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Info structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetInfo(App_Settings_Info_t* p_Settings);

/** @brief              Get the Lepton settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to System settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetLepton(App_Settings_Lepton_t* p_Settings);

/** @brief                  Update Lepton settings in the Settings Manager RAM.
 *                          This function triggers the SETTINGS_EVENT_LEPTON_CHANGED event.
 *  @param p_Settings       Pointer to Lepton settings structure
 *  @param p_ChangedSetting Optional pointer to structure to receive changed setting ID and value for event data (can be NULL if not needed)
 *  @return                 ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateLepton(App_Settings_Lepton_t* p_Settings, SettingsManager_Setting_t* p_ChangedSetting = NULL);

/** @brief              Get the WiFi settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to WiFi settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetWiFi(App_Settings_WiFi_t* p_Settings);

/** @brief              Update WiFi settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_WIFI_CHANGED event.
 *  @param p_Settings   Pointer to WiFi settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateWiFi(App_Settings_WiFi_t* p_Settings);

/** @brief              Get the Provisioning settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Provisioning settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetProvisioning(App_Settings_Provisioning_t* p_Settings);

/** @brief              Update Provisioning settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_PROVISIONING_CHANGED event.
 *  @param p_Settings   Pointer to Provisioning settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateProvisioning(App_Settings_Provisioning_t* p_Settings);

/** @brief              Get the Display settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to Display settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetDisplay(App_Settings_Display_t* p_Settings);

/** @brief              Update Display settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_DISPLAY_CHANGED event.
 *  @param p_Settings   Pointer to Display settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateDisplay(App_Settings_Display_t* p_Settings);

/** @brief              Get the HTTP Server settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to HTTP Server settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetHTTPServer(App_Settings_HTTP_Server_t* p_Settings);

/** @brief              Update HTTP Server settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_HTTP_SERVER_CHANGED event.
 *  @param p_Settings   Pointer to HTTP Server settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateHTTPServer(App_Settings_HTTP_Server_t* p_Settings);

/** @brief              Get the VISA Server settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to VISA Server settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetVISAServer(App_Settings_VISA_Server_t* p_Settings);

/** @brief              Update VISA Server settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_VISA_SERVER_CHANGED event.
 *  @param p_Settings   Pointer to VISA Server settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateVISAServer(App_Settings_VISA_Server_t* p_Settings);

/** @brief              Get the system settings from the Settings Manager RAM.
 *  @param p_Settings   Pointer to System settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_* on failure
*/
esp_err_t SettingsManager_GetSystem(App_Settings_System_t* p_Settings);

/** @brief              Update System settings in the Settings Manager RAM.
 *                      This function triggers the SETTINGS_EVENT_SYSTEM_CHANGED event.
 *  @param p_Settings   Pointer to System settings structure
 *  @return             ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_UpdateSystem(App_Settings_System_t* p_Settings);

/** @brief  Reset all settings to factory defaults.
 *          Erases NVS partition and reloads defaults.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_ResetToDefaults(void);

#endif /* SETTINGS_MANAGER_H_ */
