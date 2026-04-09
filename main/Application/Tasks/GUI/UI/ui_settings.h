/*
 * ui_settings.h
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

#ifndef UI_SETTINGS_H_
#define UI_SETTINGS_H_

#include <lvgl.h>

#include <stdint.h>

extern lv_obj_t *ui_settings_wifi_status_label;
extern lv_obj_t *ui_settings_wifi_connect_btn;

/** @brief          Initializes the settings UI.
 *  @param p_Parent Pointer to the parent object where the settings UI will be attached
 */
void ui_settings_init(lv_obj_t *p_Parent);

/** @brief  Updates flash partition usage information in the Flash settings page.
 *          Call this when the Flash settings page becomes visible to show
 *          current storage and coredump partition usage.
 *  @note   This function queries the FlashManager for current partition usage
 *          and updates the UI labels accordingly.
 */
void ui_settings_update_memory_usage(void);

#endif /* UI_SETTINGS_H_ */