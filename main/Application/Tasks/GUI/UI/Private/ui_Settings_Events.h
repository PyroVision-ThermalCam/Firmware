/*
 * ui_Settings_Events.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings UI implementation.
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

#ifndef UI_SETTINGS_EVENTS_H_
#define UI_SETTINGS_EVENTS_H_

#include <lvgl.h>

#include <stdint.h>

/** @brief          Forward declaration for memory usage update function.
 *                  Implemented in ui_Settings.cpp to update storage and
 *                  coredump partition usage displays.
 */
void ui_settings_update_flash_usage(void);

/** @brief
 */
typedef struct {
    lv_obj_t *Slider;
    lv_obj_t *Label;
} Slider_Widgets_t;

/** @brief
 */
typedef struct {
    lv_obj_t *Container;
    lv_obj_t *Page;
} Menu_Page_Result_t;

extern Slider_Widgets_t brightness_widgets;
extern Slider_Widgets_t emissivity_widgets;

extern lv_obj_t *usb_mode_switch;

/** @brief      Display brightness slider event callback to update value label.
 *  @param e    Pointer to the event object
 */
void on_Lepton_Emissivity_Slider_Callback(lv_event_t *e);

/** @brief      Display brightness slider event callback to update value label.
 *  @param e    Pointer to the event object
 */
void on_Display_Brightness_Slider_Callback(lv_event_t *e);

/** @brief      Dropdown event callback to update slider and label with selected emissivity preset value.
 *  @param e    Pointer to the event object
 */
void on_Lepton_Dropdown_Callback(lv_event_t *e);

/** @brief      Switch event callback to toggle WiFi autoconnect setting.
 *  @param e    Pointer to the event object
 */
void on_WiFi_Autoconnect_Callback(lv_event_t *e);

/** @brief      WiFi connect button callback to open WiFi connection dialog.
 *  @param e    Pointer to the event object
 */
void on_WiFi_Connect_Callback(lv_event_t *e);

/** @brief      Flash clear NVS button callback to reset all settings to factory defaults.
 *  @param e    Pointer to the event object
 */
void on_Flash_ClearNVS_Callback(lv_event_t *e);

/** @brief      Flash clear storage button callback to erase storage partition.
 *  @param e    Pointer to the event object
 */
void on_Flash_ClearStorage_Callback(lv_event_t *e);

/** @brief      Flash clear coredump button callback to erase coredump partition.
 *  @param e    Pointer to the event object
 */
void on_Flash_ClearCoredump_Callback(lv_event_t *e);

/** @brief      USB mode switch callback to enable/disable USB Mass Storage.
 *  @param e    Pointer to the event object
 */
void on_USB_Mode_Switch_Callback(lv_event_t *e);

/** @brief                  Network event handler which is used to handle network-related events such as WiFi connection changes.
 *                          This can be used to update the UI or internal state based on network events.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data);

/** @brief                  Settings event handler which is used to update the UI elements when a
 *                          settings change event is received from the Settings Manager.
 *                          This ensures that the UI always reflects the current settings values.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
void on_Settings_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data);

/** @brief                  USB event handler which is used to update the UI elements when a
 *                          USB change event is received from the USB Manager.
 *                          This ensures that the UI always reflects the current settings values.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
void on_USB_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data);

#endif /* UI_SETTINGS_EVENTS_H_ */