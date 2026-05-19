/*
 * ui_settings.cpp
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

#include <esp_event.h>
#include <esp_task_wdt.h>

#include <string>
#include <string.h>

#include "../Export/ui.h"
#include "../Export/screens/ui_Menu.h"

#include "managers.h"
#include "lepton_palette.h"
#include "ui_settings.h"
#include "ui_settings_events.h"

static lv_obj_t *memory_storage_used_label = NULL;
static lv_obj_t *memory_storage_free_label = NULL;
static lv_obj_t *memory_coredump_used_label = NULL;
static lv_obj_t *memory_coredump_total_label = NULL;
static lv_obj_t *root_page;

static const char *ui_settings_timezone[] = {
    "UTC0",
    "GMT0BST,M3.5.0/1,M10.5.0",
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "MSK-3",
    "EST5EDT,M3.2.0,M11.1.0",
    "CST6CDT,M3.2.0,M11.1.0",
    "MST7MDT,M3.2.0,M11.1.0",
    "PST8PDT,M3.2.0,M11.1.0",
    "JST-9",
    "CST-8",
    "IST-5:30",
    "AEST-10AEDT,M10.1.0,M4.1.0/3",
};

Slider_Widgets_t brightness_widgets;
Slider_Widgets_t timeout_widgets;
Slider_Widgets_t emissivity_widgets;
Slider_Widgets_t jpeg_quality_widgets;

lv_obj_t *cont;
lv_obj_t *section;
lv_obj_t *about_Page;
lv_obj_t *wifi_Page;
lv_obj_t *display_Page;
lv_obj_t *lepton_Page;
lv_obj_t *memory_Page;
lv_obj_t *settings_Menu;
lv_obj_t *emissivity_Dropdown;
lv_obj_t *usb_Page;
lv_obj_t *system_Page;
lv_obj_t *image_Page;
lv_obj_t *calibration_Page;
lv_obj_t *usb_mode_switch;
lv_obj_t *usb_uvc_switch;
lv_obj_t *image_format_dropdown;
lv_obj_t *jpeg_quality_row;
lv_obj_t *ui_settings_wifi_status_label;
lv_obj_t *ui_settings_wifi_connect_btn;
lv_obj_t *ui_palette_dropdown;

static const char *TAG = "ui_settings";

/** @brief      Event handler for menu page changes to control Save button visibility.
 *  @note       Hides Save button when USB or Flash settings are active because
 *              filesystem is locked during USB mode and Flash operations don't
 *              require explicit saving.
 *  @param e    Pointer to the event
 */
static void on_Menu_PageChanged(lv_event_t *e)
{
    lv_obj_t *menu_obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_obj_t *cur_page = lv_menu_get_cur_main_page(menu_obj);

    if (cur_page == NULL) {
        return;
    }

    /* Hide Save button on USB and Flash pages */
    if ((cur_page == usb_Page) || (cur_page == memory_Page)) {
        lv_obj_add_flag(ui_Button_Menu_Button4, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(ui_Button_Menu_Button4, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *ui_Settings_Create_Menu_Container(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(container, 255, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    return container;
}

static lv_obj_t *ui_Settings_Create_Row(lv_obj_t *parent, lv_flex_align_t main_align)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_clear_flag(row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_size(row, LV_PCT(100), 28);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_row(row, 4, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    return row;
}

static lv_obj_t *ui_Settings_Create_Compact_Slider(lv_obj_t *parent, const char *label_text,
                                                   int min_val, int max_val, int init_val, Slider_Widgets_t *widgets)
{
    char Buffer[8];

    if (widgets == NULL) {
        return NULL;
    }

    lv_obj_t *label_row = ui_Settings_Create_Row(parent, LV_FLEX_ALIGN_START);
    lv_obj_t *label = lv_label_create(label_row);
    lv_label_set_text(label, label_text);
    lv_obj_set_width(label, LV_PCT(50));
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    snprintf(Buffer, sizeof(Buffer), "%i", init_val);
    lv_obj_t *value_label = lv_label_create(label_row);
    lv_label_set_text(value_label, Buffer);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(value_label, 40);
    lv_obj_set_height(value_label, 24);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(value_label, lv_color_hex(0x7B3FF0), 0);
    lv_obj_set_style_bg_opa(value_label, 255, 0);
    lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
    lv_obj_set_style_radius(value_label, 6, 0);
    lv_obj_set_style_pad_all(value_label, 5, 0);
    lv_obj_clear_flag(value_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_margin_left(value_label, 10, 0);

    lv_obj_t *slider_row = ui_Settings_Create_Row(parent, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *slider = lv_slider_create(slider_row);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);

    widgets->Slider = slider;
    widgets->Label = value_label;
    lv_obj_set_user_data(slider, widgets);

    lv_slider_set_range(slider, min_val, max_val);
    lv_slider_set_value(slider, init_val, LV_ANIM_OFF);
    lv_obj_set_width(slider, LV_PCT(90));
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x7B3FF0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFF9500), LV_PART_KNOB);

    return slider;
}

static Menu_Page_Result_t ui_Settings_Create_Menu_Page_With_Container(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, NULL);
    lv_obj_t *section = lv_menu_section_create(page);
    lv_obj_t *container = ui_Settings_Create_Menu_Container(section);

    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(page, 0, 0);

    lv_obj_set_scrollbar_mode(section, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(section, LV_DIR_VER);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 0, 0);
    lv_obj_set_style_pad_right(section, 8, LV_PART_SCROLLBAR);

    return {container, page};
}

static lv_obj_t *ui_Settings_Create_Text(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *label = NULL;

    lv_obj_t *obj = lv_menu_cont_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_EDITED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, 255, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_set_style_margin_bottom(obj, 4, 0);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_EDITED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_EDITED);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(obj, 0, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(obj, 15, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(obj, 0, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_opa(obj, 180, LV_STATE_PRESSED);
    lv_obj_set_size(obj, LV_PCT(95), 36);

    if (txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_flex_grow(label, 1);
    }

    return obj;
}

/** @brief          Creates the About page.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_About_Page(lv_obj_t *p_parent)
{
    lv_obj_t *cont;
    lv_obj_t *section;
    lv_obj_t *about_page;
    lv_obj_t *about_container;
    Menu_Page_Result_t about_result;
    lv_obj_t *sub_software_info_page;
    lv_obj_t *sub_legal_info_page;
    char Buffer[64];

    about_result = ui_Settings_Create_Menu_Page_With_Container(p_parent);
    about_container = about_result.Container;
    about_page = about_result.Page;

    lv_menu_separator_create(about_page);

    sub_software_info_page = lv_menu_page_create(p_parent, NULL);
    section = lv_menu_section_create(sub_software_info_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 255, 0);
    lv_obj_set_style_pad_all(section, 8, 0);
    ui_Settings_Create_Text(section, "PyroVision Firmware");

    __builtin_memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Version %u.%u.%u", 1, 0, 0);
    ui_Settings_Create_Text(section, Buffer);

    __builtin_memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Platform: %s", CONFIG_IDF_TARGET);
    ui_Settings_Create_Text(section, Buffer);

    __builtin_memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "LVGL Version: %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH);
    ui_Settings_Create_Text(section, Buffer);

    sub_legal_info_page = lv_menu_page_create(p_parent, NULL);
    section = lv_menu_section_create(sub_legal_info_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 255, 0);
    lv_obj_set_style_pad_all(section, 8, 0);

    lv_obj_t *license_label = lv_label_create(section);
    lv_label_set_text(license_label,
                      "(c) 2026 PyroVision Project\n"
                      "Licensed under GNU GPL v3\n"
                      "\n"
                      "This program is free software.\n"
                      "See LICENSE file for details.");
    lv_obj_set_width(license_label, LV_PCT(95));
    lv_obj_set_style_text_color(license_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(license_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(license_label, LV_LABEL_LONG_WRAP);

    cont = ui_Settings_Create_Text(about_container, "Software information");
    lv_menu_set_load_page_event(p_parent, cont, sub_software_info_page);
    cont = ui_Settings_Create_Text(about_container, "Legal information");
    lv_menu_set_load_page_event(p_parent, cont, sub_legal_info_page);

    return about_page;
}

/** @brief          Creates the WiFi settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_WiFi_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t WiFiResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *WiFiContainer = WiFiResult.Container;
    lv_obj_t *WiFiPage = WiFiResult.Page;
    Settings_WiFi_t WiFiSettings;

    SettingsManager_GetWiFi(&WiFiSettings);

    lv_obj_t *wifi_row1 = ui_Settings_Create_Row(WiFiContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *wifi_label = lv_label_create(wifi_row1);
    lv_label_set_text(wifi_label, "Autoconnect");
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);

    /* Separator */
    lv_obj_t *separator1 = lv_obj_create(WiFiContainer);
    lv_obj_set_size(separator1, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator1, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator1, 0, 0);
    lv_obj_set_style_pad_all(separator1, 0, 0);
    lv_obj_set_style_margin_top(separator1, 12, 0);
    lv_obj_set_style_margin_bottom(separator1, 12, 0);

    lv_obj_t *wifi_switch = lv_switch_create(wifi_row1);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0x7B3FF0), 0);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0xFF9500), LV_PART_KNOB);
    lv_obj_add_event_cb(wifi_switch, on_WiFi_Autoconnect_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    if (WiFiSettings.AutoConnect) {
        lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(wifi_switch, LV_STATE_CHECKED);
    }

    lv_obj_t *wifi_row2 = ui_Settings_Create_Row(WiFiContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *ssid_label = lv_label_create(wifi_row2);
    lv_label_set_text(ssid_label, "Status");
    lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
    ui_settings_wifi_status_label = lv_label_create(wifi_row2);
    lv_label_set_text(ui_settings_wifi_status_label, "Offline");
    lv_obj_set_style_text_color(ui_settings_wifi_status_label, lv_color_hex(0xFF9500), 0);

    /* WiFi Connect Button */
    lv_obj_t *wifi_btn_row = ui_Settings_Create_Row(WiFiContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(wifi_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(wifi_btn_row, 12, 0);
    lv_obj_set_style_pad_bottom(wifi_btn_row, 8, 0);
    ui_settings_wifi_connect_btn = lv_btn_create(wifi_btn_row);
    lv_obj_set_size(ui_settings_wifi_connect_btn, 140, 36);
    lv_obj_set_style_bg_color(ui_settings_wifi_connect_btn, lv_color_hex(0xFF9500), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_settings_wifi_connect_btn, 6, 0);
    lv_obj_set_style_shadow_width(ui_settings_wifi_connect_btn, 0, 0);
    lv_obj_add_event_cb(ui_settings_wifi_connect_btn, on_WiFi_Connect_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_btn_label = lv_label_create(ui_settings_wifi_connect_btn);
    lv_label_set_text(wifi_btn_label, "Connect WiFi");
    lv_obj_set_style_text_color(wifi_btn_label, lv_color_white(), 0);
    lv_obj_center(wifi_btn_label);

    /* Separator */
    lv_obj_t *separator2 = lv_obj_create(WiFiContainer);
    lv_obj_set_size(separator2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator2, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator2, 0, 0);
    lv_obj_set_style_pad_all(separator2, 0, 0);
    lv_obj_set_style_margin_top(separator2, 12, 0);
    lv_obj_set_style_margin_bottom(separator2, 12, 0);

    /* Section label */
    lv_obj_t *section_label = lv_label_create(WiFiContainer);
    lv_label_set_text(section_label, "WiFi Credentials");
    lv_obj_set_style_text_color(section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(section_label, 0, 0);
    lv_obj_set_style_pad_bottom(section_label, 8, 0);

    /* Description */
    lv_obj_t *desc_label = lv_label_create(WiFiContainer);
    lv_label_set_text(desc_label, "Removes the stored SSID and password from the device. "
                                  "On next boot the provisioning access point will start.");
    lv_obj_set_width(desc_label, LV_PCT(95));
    lv_obj_set_style_text_color(desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_bottom(desc_label, 16, 0);

    /* Clear button */
    lv_obj_t *btn_row = ui_Settings_Create_Row(WiFiContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(btn_row, 4, 0);
    lv_obj_set_style_pad_bottom(btn_row, 8, 0);
    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_size(clear_btn, 160, 36);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xC0392B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(clear_btn, 0, 0);
    lv_obj_add_event_cb(clear_btn, on_WiFi_ClearCredentials_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_btn_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_btn_label, "Clear Credentials");
    lv_obj_set_style_text_color(clear_btn_label, lv_color_white(), 0);
    lv_obj_center(clear_btn_label);

    return WiFiPage;
}

/** @brief          Creates the display settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_Display_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t DisplayResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *DisplayContainer = DisplayResult.Container;
    lv_obj_t *DisplayPage = DisplayResult.Page;
    Settings_Display_t DisplaySettings;

    SettingsManager_GetDisplay(&DisplaySettings);

    lv_obj_t *brightness_slider = ui_Settings_Create_Compact_Slider(DisplayContainer, "Brightness", 0, 100,
                                                                    DisplaySettings.Brightness,
                                                                    &brightness_widgets);

    lv_obj_add_event_cb(brightness_slider, on_Display_Brightness_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, on_Display_Brightness_Slider_Callback, LV_EVENT_RELEASED, NULL);

    /* Separator */
    lv_obj_t *separator = lv_obj_create(DisplayContainer);
    lv_obj_set_size(separator, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_set_style_margin_top(separator, 12, 0);
    lv_obj_set_style_margin_bottom(separator, 12, 0);

    lv_obj_t *timeout_slider = ui_Settings_Create_Compact_Slider(DisplayContainer, "Timeout", 0, 60,
                                                                    DisplaySettings.Timeout,
                                                                    &timeout_widgets);

    lv_obj_add_event_cb(timeout_slider, on_Display_Timeout_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(timeout_slider, on_Display_Timeout_Slider_Callback, LV_EVENT_RELEASED, NULL);

    return DisplayPage;
}

/** @brief          Creates the Lepton settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_Lepton_Page(lv_obj_t *p_Menu)
{
    std::string Buffer;
    Menu_Page_Result_t LeptonResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *LeptonContainer = LeptonResult.Container;
    lv_obj_t *LeptonPage = LeptonResult.Page;
    Settings_Lepton_t LeptonSettings;

    SettingsManager_GetLepton(&LeptonSettings);

    for (size_t i = 0; i < LeptonSettings.EmissivityPresetsCount; i++) {
        Buffer += LeptonSettings.EmissivityPresets[i].Description + std::string("\n");
    }

    lv_obj_t *emissivity_slider = ui_Settings_Create_Compact_Slider(LeptonContainer, "Emissivity", 0, 100,
                                                                    LeptonSettings.CurrentEmissivity, &emissivity_widgets);

    lv_obj_add_event_cb(emissivity_slider, on_Lepton_Emissivity_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(emissivity_slider, on_Lepton_Emissivity_Slider_Callback, LV_EVENT_RELEASED, NULL);

    lv_obj_t *dropdown_row = ui_Settings_Create_Row(LeptonContainer, LV_FLEX_ALIGN_CENTER);
    emissivity_Dropdown = lv_dropdown_create(dropdown_row);
    lv_obj_set_width(emissivity_Dropdown, LV_PCT(95));
    lv_obj_set_style_bg_color(emissivity_Dropdown, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_color(emissivity_Dropdown, lv_color_hex(0xFF9500), LV_PART_MAIN);
    lv_obj_set_style_border_width(emissivity_Dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(emissivity_Dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(emissivity_Dropdown, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(emissivity_Dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(emissivity_Dropdown, lv_color_hex(0xFF9500), LV_PART_SELECTED);
    lv_obj_set_style_text_color(emissivity_Dropdown, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_user_data(emissivity_Dropdown, &emissivity_widgets);
    lv_obj_add_event_cb(emissivity_Dropdown, on_Lepton_Dropdown_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_dropdown_set_options(emissivity_Dropdown, Buffer.c_str());
    lv_dropdown_set_selected(emissivity_Dropdown, 0);

    /* Separator */
    lv_obj_t *separator = lv_obj_create(LeptonContainer);
    lv_obj_set_size(separator, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_set_style_margin_top(separator, 12, 0);
    lv_obj_set_style_margin_bottom(separator, 12, 0);

    /* Color Palette */
    lv_obj_t *palette_label_row = ui_Settings_Create_Row(LeptonContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *palette_label = lv_label_create(palette_label_row);
    lv_label_set_text(palette_label, "Color Palette");
    lv_obj_set_style_text_color(palette_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(palette_label, &lv_font_montserrat_14, 0);

    lv_obj_t *palette_row = ui_Settings_Create_Row(LeptonContainer, LV_FLEX_ALIGN_CENTER);
    ui_palette_dropdown = lv_dropdown_create(palette_row);
    lv_obj_set_width(ui_palette_dropdown, LV_PCT(95));
    lv_obj_set_style_bg_color(ui_palette_dropdown, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_palette_dropdown, lv_color_hex(0xFF9500), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_palette_dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_palette_dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_palette_dropdown, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_palette_dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_palette_dropdown, lv_color_hex(0xFF9500), LV_PART_SELECTED);
    lv_obj_set_style_text_color(ui_palette_dropdown, lv_color_white(), LV_PART_SELECTED);
    lv_dropdown_set_options(ui_palette_dropdown, []() -> const char * {
        static std::string Options;
        Options.clear();
        for (size_t i = 0; i < LEPTON_PALETTE_COUNT; i++) {
            if (i > 0) {
                Options += "\n";
            }
            Options += Lepton_Palette_Names[i];
        }
        return Options.c_str();
    }());
    lv_dropdown_set_selected(ui_palette_dropdown, static_cast<uint16_t>(LeptonSettings.Palette));
    lv_obj_add_event_cb(ui_palette_dropdown, on_Lepton_Palette_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    /* Separator */
    lv_obj_t *separator2 = lv_obj_create(LeptonContainer);
    lv_obj_set_size(separator2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator2, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator2, 0, 0);
    lv_obj_set_style_pad_all(separator2, 0, 0);
    lv_obj_set_style_margin_top(separator2, 12, 0);
    lv_obj_set_style_margin_bottom(separator2, 12, 0);

    /* Lepton reset Connect Button */
    lv_obj_t *lepton_reset_btn_row = ui_Settings_Create_Row(LeptonContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(lepton_reset_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(lepton_reset_btn_row, 12, 0);
    lv_obj_set_style_pad_bottom(lepton_reset_btn_row, 8, 0);
    lv_obj_t *ui_settings_lepton_reset_btn = lv_btn_create(lepton_reset_btn_row);
    lv_obj_set_size(ui_settings_lepton_reset_btn, 140, 36);
    lv_obj_set_style_bg_color(ui_settings_lepton_reset_btn, lv_color_hex(0xFF9500), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_settings_lepton_reset_btn, 6, 0);
    lv_obj_set_style_shadow_width(ui_settings_lepton_reset_btn, 0, 0);
    lv_obj_add_event_cb(ui_settings_lepton_reset_btn, on_Lepton_Reset_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lepton_reset_btn_label = lv_label_create(ui_settings_lepton_reset_btn);
    lv_label_set_text(lepton_reset_btn_label, "Reset Lepton");
    lv_obj_set_style_text_color(lepton_reset_btn_label, lv_color_white(), 0);
    lv_obj_center(lepton_reset_btn_label);

    return LeptonPage;
}

/** @brief          Creates the USB settings page.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_USB_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t USBResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *USBContainer = USBResult.Container;
    lv_obj_t *USBPage = USBResult.Page;

    lv_obj_t *usb_section_label = lv_label_create(USBContainer);
    lv_label_set_text(usb_section_label, "USB Mass Storage");
    lv_obj_set_style_text_color(usb_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(usb_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(usb_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(usb_section_label, 8, 0);

    lv_obj_t *usb_desc_label = lv_label_create(USBContainer);
    lv_label_set_text(usb_desc_label, "Expose storage via USB to PC");
    lv_obj_set_style_text_color(usb_desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(usb_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(usb_desc_label, 8, 0);

    lv_obj_t *usb_warning_label = lv_label_create(USBContainer);
    lv_label_set_text(usb_warning_label, "  App cannot save while USB is active!");
    lv_obj_set_style_text_color(usb_warning_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(usb_warning_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_bottom(usb_warning_label, 12, 0);

    lv_obj_t *usb_switch_row = ui_Settings_Create_Row(USBContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_top(usb_switch_row, 4, 0);
    lv_obj_set_style_pad_bottom(usb_switch_row, 4, 0);

    lv_obj_t *usb_mode_label = lv_label_create(usb_switch_row);
    lv_label_set_text(usb_mode_label, "MSC Mode");
    lv_obj_set_style_text_color(usb_mode_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(usb_mode_label, &lv_font_montserrat_14, 0);

    usb_mode_switch = lv_switch_create(usb_switch_row);
    lv_obj_set_size(usb_mode_switch, 50, 25);
    lv_obj_set_style_bg_color(usb_mode_switch, lv_color_hex(0x7B3FF0), 0);
    lv_obj_set_style_bg_color(usb_mode_switch, lv_color_hex(0xFF9500), LV_PART_KNOB);
    lv_obj_add_event_cb(usb_mode_switch, on_USB_Mode_Switch_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_state(usb_mode_switch, LV_STATE_DISABLED);

    /* Separator */
    lv_obj_t *separator = lv_obj_create(USBContainer);
    lv_obj_set_size(separator, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_set_style_margin_top(separator, 12, 0);
    lv_obj_set_style_margin_bottom(separator, 12, 0);

    /* UVC Section */
    lv_obj_t *uvc_section_label = lv_label_create(USBContainer);
    lv_label_set_text(uvc_section_label, "USB Video Class");
    lv_obj_set_style_text_color(uvc_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(uvc_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(uvc_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(uvc_section_label, 8, 0);

    lv_obj_t *uvc_desc_label = lv_label_create(USBContainer);
    lv_label_set_text(uvc_desc_label, "Stream thermal camera via USB");
    lv_obj_set_style_text_color(uvc_desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(uvc_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(uvc_desc_label, 12, 0);

    lv_obj_t *uvc_switch_row = ui_Settings_Create_Row(USBContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_top(uvc_switch_row, 4, 0);
    lv_obj_set_style_pad_bottom(uvc_switch_row, 4, 0);

    lv_obj_t *uvc_mode_label = lv_label_create(uvc_switch_row);
    lv_label_set_text(uvc_mode_label, "UVC Mode");
    lv_obj_set_style_text_color(uvc_mode_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(uvc_mode_label, &lv_font_montserrat_14, 0);

    usb_uvc_switch = lv_switch_create(uvc_switch_row);
    lv_obj_set_size(usb_uvc_switch, 50, 25);
    lv_obj_set_style_bg_color(usb_uvc_switch, lv_color_hex(0x7B3FF0), 0);
    lv_obj_set_style_bg_color(usb_uvc_switch, lv_color_hex(0xFF9500), LV_PART_KNOB);
    lv_obj_add_event_cb(usb_uvc_switch, on_USB_UVC_Switch_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_state(usb_uvc_switch, LV_STATE_DISABLED);

    return USBPage;
}

/** @brief          Creates the Flash settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_Memory_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t MemoryResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *MemoryContainer = MemoryResult.Container;
    lv_obj_t *MemoryPage = MemoryResult.Page;

    lv_obj_t *nvs_section_label = lv_label_create(MemoryContainer);
    lv_label_set_text(nvs_section_label, "NVS Settings");
    lv_obj_set_style_text_color(nvs_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(nvs_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(nvs_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(nvs_section_label, 8, 0);

    lv_obj_t *nvs_desc_label = lv_label_create(MemoryContainer);
    lv_label_set_text(nvs_desc_label, "Factory reset");
    lv_obj_set_style_text_color(nvs_desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(nvs_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(nvs_desc_label, 8, 0);

    lv_obj_t *nvs_btn_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(nvs_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(nvs_btn_row, 4, 0);
    lv_obj_set_style_pad_bottom(nvs_btn_row, 4, 0);
    lv_obj_t *nvs_clear_btn = lv_btn_create(nvs_btn_row);
    lv_obj_set_size(nvs_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(nvs_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(nvs_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(nvs_clear_btn, 0, 0);
    lv_obj_add_event_cb(nvs_clear_btn, on_Memory_ClearNVS_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nvs_btn_label = lv_label_create(nvs_clear_btn);
    lv_label_set_text(nvs_btn_label, "Reset");
    lv_obj_set_style_text_color(nvs_btn_label, lv_color_white(), 0);
    lv_obj_center(nvs_btn_label);

    /* Separator */
    lv_obj_t *separator1 = lv_obj_create(MemoryContainer);
    lv_obj_set_size(separator1, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator1, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator1, 0, 0);
    lv_obj_set_style_pad_all(separator1, 0, 0);
    lv_obj_set_style_margin_top(separator1, 12, 0);
    lv_obj_set_style_margin_bottom(separator1, 12, 0);

    lv_obj_t *storage_section_label = lv_label_create(MemoryContainer);
    lv_label_set_text(storage_section_label, "Storage Partition");
    lv_obj_set_style_text_color(storage_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(storage_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(storage_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(storage_section_label, 8, 0);

    lv_obj_t *storage_info_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *storage_used_label = lv_label_create(storage_info_row);
    lv_label_set_text(storage_used_label, "Used:");
    lv_obj_set_style_text_color(storage_used_label, lv_color_white(), 0);
    memory_storage_used_label = lv_label_create(storage_info_row);
    lv_label_set_text(memory_storage_used_label, "-- KB");
    lv_obj_set_style_text_color(memory_storage_used_label, lv_color_hex(0xB998FF), 0);

    lv_obj_t *storage_free_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *storage_free_label = lv_label_create(storage_free_row);
    lv_label_set_text(storage_free_label, "Free:");
    lv_obj_set_style_text_color(storage_free_label, lv_color_white(), 0);
    memory_storage_free_label = lv_label_create(storage_free_row);
    lv_label_set_text(memory_storage_free_label, "-- KB");
    lv_obj_set_style_text_color(memory_storage_free_label, lv_color_hex(0xB998FF), 0);

    lv_obj_t *storage_btn_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(storage_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(storage_btn_row, 8, 0);
    lv_obj_set_style_pad_bottom(storage_btn_row, 8, 0);
    lv_obj_t *storage_clear_btn = lv_btn_create(storage_btn_row);
    lv_obj_set_size(storage_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(storage_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(storage_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(storage_clear_btn, 0, 0);
    lv_obj_add_event_cb(storage_clear_btn, on_Memory_ClearStorage_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *storage_btn_label = lv_label_create(storage_clear_btn);
    lv_label_set_text(storage_btn_label, "Clear");
    lv_obj_set_style_text_color(storage_btn_label, lv_color_white(), 0);
    lv_obj_center(storage_btn_label);

    /* Separator */
    lv_obj_t *separator2 = lv_obj_create(MemoryContainer);
    lv_obj_set_size(separator2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator2, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator2, 0, 0);
    lv_obj_set_style_pad_all(separator2, 0, 0);
    lv_obj_set_style_margin_top(separator2, 12, 0);
    lv_obj_set_style_margin_bottom(separator2, 12, 0);

    lv_obj_t *coredump_section_label = lv_label_create(MemoryContainer);
    lv_label_set_text(coredump_section_label, "Coredump Partition");
    lv_obj_set_style_text_color(coredump_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(coredump_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(coredump_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(coredump_section_label, 8, 0);

    lv_obj_t *coredump_info_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *coredump_used_label = lv_label_create(coredump_info_row);
    lv_label_set_text(coredump_used_label, "Used:");
    lv_obj_set_style_text_color(coredump_used_label, lv_color_white(), 0);
    memory_coredump_used_label = lv_label_create(coredump_info_row);
    lv_label_set_text(memory_coredump_used_label, "-- KB");
    lv_obj_set_style_text_color(memory_coredump_used_label, lv_color_hex(0xB998FF), 0);

    lv_obj_t *coredump_total_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t *coredump_total_label = lv_label_create(coredump_total_row);
    lv_label_set_text(coredump_total_label, "Total:");
    lv_obj_set_style_text_color(coredump_total_label, lv_color_white(), 0);
    memory_coredump_total_label = lv_label_create(coredump_total_row);
    lv_label_set_text(memory_coredump_total_label, "-- KB");
    lv_obj_set_style_text_color(memory_coredump_total_label, lv_color_hex(0xB998FF), 0);

    lv_obj_t *coredump_btn_row = ui_Settings_Create_Row(MemoryContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(coredump_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(coredump_btn_row, 8, 0);
    lv_obj_set_style_pad_bottom(coredump_btn_row, 16, 0);
    lv_obj_t *coredump_clear_btn = lv_btn_create(coredump_btn_row);
    lv_obj_set_size(coredump_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(coredump_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(coredump_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(coredump_clear_btn, 0, 0);
    lv_obj_add_event_cb(coredump_clear_btn, on_Memory_ClearCoredump_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *coredump_btn_label = lv_label_create(coredump_clear_btn);
    lv_label_set_text(coredump_btn_label, "Clear");
    lv_obj_set_style_text_color(coredump_btn_label, lv_color_white(), 0);
    lv_obj_center(coredump_btn_label);

    return MemoryPage;
}

/** @brief          Creates the Image/Capture settings page.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_Image_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t ImageResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *ImageContainer = ImageResult.Container;
    lv_obj_t *ImagePage = ImageResult.Page;
    Settings_Image_t ImageSettings;

    SettingsManager_GetImage(&ImageSettings);

    /* Section Label */
    lv_obj_t *image_section_label = lv_label_create(ImageContainer);
    lv_label_set_text(image_section_label, "Image Format");
    lv_obj_set_style_text_color(image_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(image_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(image_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(image_section_label, 8, 0);

    /* Format Dropdown */
    lv_obj_t *format_dropdown_row = ui_Settings_Create_Row(ImageContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(format_dropdown_row, LV_SIZE_CONTENT);
    image_format_dropdown = lv_dropdown_create(format_dropdown_row);
    lv_obj_set_width(image_format_dropdown, LV_PCT(95));
    lv_obj_set_style_bg_color(image_format_dropdown, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_color(image_format_dropdown, lv_color_hex(0xFF9500), LV_PART_MAIN);
    lv_obj_set_style_border_width(image_format_dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(image_format_dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(image_format_dropdown, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(image_format_dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(image_format_dropdown, lv_color_hex(0xFF9500), LV_PART_SELECTED);
    lv_obj_set_style_text_color(image_format_dropdown, lv_color_white(), LV_PART_SELECTED);
    lv_dropdown_set_options(image_format_dropdown, "JPEG\nPNG\nRAW\nBitmap");
    lv_dropdown_set_selected(image_format_dropdown, static_cast<uint16_t>(ImageSettings.Format));
    lv_obj_add_event_cb(image_format_dropdown, on_Image_Format_Dropdown_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    /* Separator */
    lv_obj_t *separator = lv_obj_create(ImageContainer);
    lv_obj_set_size(separator, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_set_style_margin_top(separator, 16, 0);
    lv_obj_set_style_margin_bottom(separator, 16, 0);

    /* JPEG Quality Section — wrapped in a container so it can be shown/hidden atomically */
    jpeg_quality_row = lv_obj_create(ImageContainer);
    lv_obj_set_size(jpeg_quality_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(jpeg_quality_row, 0, 0);
    lv_obj_set_style_border_width(jpeg_quality_row, 0, 0);
    lv_obj_set_style_pad_all(jpeg_quality_row, 0, 0);
    lv_obj_clear_flag(jpeg_quality_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(jpeg_quality_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(jpeg_quality_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *quality_section_label = lv_label_create(jpeg_quality_row);
    lv_label_set_text(quality_section_label, "JPEG Settings");
    lv_obj_set_style_text_color(quality_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(quality_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(quality_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(quality_section_label, 8, 0);

    /* JPEG Quality Slider (only visible when JPEG is selected) */
    lv_obj_t *quality_slider = ui_Settings_Create_Compact_Slider(jpeg_quality_row, "Quality", 1, 100,
                                                                 ImageSettings.JpegQuality, &jpeg_quality_widgets);
    lv_obj_add_event_cb(quality_slider, on_Image_JpegQuality_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    /* Show/hide quality section based on current format */
    if (ImageSettings.Format != IMAGE_FORMAT_JPEG) {
        lv_obj_add_flag(jpeg_quality_row, LV_OBJ_FLAG_HIDDEN);
    }

    /* Info text */
    lv_obj_t *info_label = lv_label_create(ImageContainer);
    lv_label_set_text(info_label,
                      "JPEG: Compressed, small files\n"
                      "PNG: Lossless (not yet implemented)\n"
                      "RAW: Uncompressed RGB data\n"
                      "Bitmap: BMP file format");
    lv_obj_set_width(info_label, LV_PCT(95));
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_margin_top(info_label, 16, 0);

    return ImagePage;
}

/** @brief          Creates the Calibration settings page.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_Calibration_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t CalibResult = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *CalibContainer = CalibResult.Container;
    lv_obj_t *CalibPage = CalibResult.Page;
    Settings_Calibration_t CalibSettings;

    SettingsManager_GetCalibration(&CalibSettings);

    /* Section label */
    lv_obj_t *calib_section_label = lv_label_create(CalibContainer);
    lv_label_set_text(calib_section_label, "Room Temperature");
    lv_obj_set_style_text_color(calib_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(calib_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(calib_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(calib_section_label, 8, 0);

    lv_obj_t *calib_desc_label = lv_label_create(CalibContainer);
    lv_label_set_text(calib_desc_label, "Current ambient temperature");
    lv_obj_set_style_text_color(calib_desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(calib_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(calib_desc_label, 16, 0);

    /* Spinbox row: [-] [spinbox] [+] [°C] */
    lv_obj_t *spinbox_row = ui_Settings_Create_Row(CalibContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(spinbox_row, 48);
    lv_obj_set_style_pad_top(spinbox_row, 4, 0);
    lv_obj_set_style_pad_bottom(spinbox_row, 4, 0);
    lv_obj_set_style_pad_column(spinbox_row, 8, 0);

    /* Decrement button */
    lv_obj_t *dec_btn = lv_btn_create(spinbox_row);
    lv_obj_set_size(dec_btn, 36, 36);
    lv_obj_set_style_bg_color(dec_btn, lv_color_hex(0x7B3FF0), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(dec_btn, 6, 0);
    lv_obj_set_style_shadow_width(dec_btn, 0, 0);
    lv_obj_t *dec_label = lv_label_create(dec_btn);
    lv_label_set_text(dec_label, "-");
    lv_obj_set_style_text_color(dec_label, lv_color_white(), 0);
    lv_obj_center(dec_label);

    /* Spinbox */
    lv_obj_t *spinbox = lv_spinbox_create(spinbox_row);
    lv_spinbox_set_range(spinbox, -40, 80);
    lv_spinbox_set_digit_format(spinbox, 2, 0);
    lv_spinbox_set_value(spinbox, static_cast<int32_t>(CalibSettings.RoomTemperature));
    lv_obj_set_width(spinbox, 80);
    lv_obj_set_style_bg_color(spinbox, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_text_color(spinbox, lv_color_white(), 0);
    lv_obj_set_style_border_color(spinbox, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_border_width(spinbox, 2, 0);
    lv_obj_set_style_radius(spinbox, 6, 0);
    lv_obj_set_style_pad_all(spinbox, 8, 0);
    lv_obj_add_event_cb(spinbox, on_Calibration_RoomTemp_Changed_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    /* Increment button */
    lv_obj_t *inc_btn = lv_btn_create(spinbox_row);
    lv_obj_set_size(inc_btn, 36, 36);
    lv_obj_set_style_bg_color(inc_btn, lv_color_hex(0x7B3FF0), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(inc_btn, 6, 0);
    lv_obj_set_style_shadow_width(inc_btn, 0, 0);
    lv_obj_t *inc_label = lv_label_create(inc_btn);
    lv_label_set_text(inc_label, "+");
    lv_obj_set_style_text_color(inc_label, lv_color_white(), 0);
    lv_obj_center(inc_label);

    /* Unit label */
    lv_obj_t *unit_label = lv_label_create(spinbox_row);
    lv_label_set_text(unit_label, "\xC2\xB0""C");
    lv_obj_set_style_text_color(unit_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_14, 0);

    /* Button callbacks — pass spinbox as user_data */
    lv_obj_add_event_cb(dec_btn, [](lv_event_t *e) {
        lv_spinbox_decrement(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, spinbox);

    lv_obj_add_event_cb(inc_btn, [](lv_event_t *e) {
        lv_spinbox_increment(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, spinbox);

    return CalibPage;
}

/** @brief          Creates the System settings page.
 *                  Currently contains a timezone selection dropdown.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t *ui_Settings_Create_System_Page(lv_obj_t *p_Menu)
{
    Settings_System_t SystemSettings;
    Menu_Page_Result_t Result = ui_Settings_Create_Menu_Page_With_Container(p_Menu);
    lv_obj_t *Container = Result.Container;

    SettingsManager_GetSystem(&SystemSettings);

    /* ── Timezone ── */
    lv_obj_t *tz_label = lv_label_create(Container);
    lv_label_set_text(tz_label, "Timezone");
    lv_obj_set_style_text_color(tz_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(tz_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_bottom(tz_label, 8, 0);

    lv_obj_t *tz_desc = lv_label_create(Container);
    lv_label_set_text(tz_desc, "Timezone used for timestamps and the on-screen clock.");
    lv_obj_set_width(tz_desc, LV_PCT(95));
    lv_obj_set_style_text_color(tz_desc, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(tz_desc, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(tz_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_bottom(tz_desc, 12, 0);

    /* Timezone dropdown — labels MUST stay in sync with TIMEZONE_POSIX[] in
     * ui_settings_events.cpp.  One entry per line, same order, same count. */
    lv_obj_t *tz_dropdown = lv_dropdown_create(Container);
    lv_obj_set_width(tz_dropdown, LV_PCT(95));
    lv_obj_set_style_bg_color(tz_dropdown, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_color(tz_dropdown, lv_color_hex(0xFF9500), LV_PART_MAIN);
    lv_obj_set_style_border_width(tz_dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tz_dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(tz_dropdown, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(tz_dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tz_dropdown, lv_color_hex(0xFF9500), LV_PART_SELECTED);
    lv_obj_set_style_text_color(tz_dropdown, lv_color_white(), LV_PART_SELECTED);
    lv_dropdown_set_options(tz_dropdown,
                            "UTC\n"
                            "Europe/London\n"
                            "Europe/Berlin (CET)\n"
                            "Europe/Helsinki (EET)\n"
                            "Europe/Moscow\n"
                            "America/New_York (ET)\n"
                            "America/Chicago (CT)\n"
                            "America/Denver (MT)\n"
                            "America/Los_Angeles (PT)\n"
                            "Asia/Tokyo (JST)\n"
                            "Asia/Shanghai (CST)\n"
                            "Asia/Kolkata (IST)\n"
                            "Australia/Sydney (AEST)");

    uint16_t Selected = 0;
    for (Selected = 0; Selected < sizeof(ui_settings_timezone) / sizeof(ui_settings_timezone[0]); Selected++) {
        if (strncmp(SystemSettings.Timezone, ui_settings_timezone[Selected], sizeof(SystemSettings.Timezone)) == 0) {
            break;
        }
    }
    lv_dropdown_set_selected(tz_dropdown, Selected);

    lv_obj_add_event_cb(tz_dropdown, on_System_Timezone_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    return Result.Page;
}

void ui_settings_init(lv_obj_t *p_Parent)
{
    settings_Menu = lv_menu_create(p_Parent);

    lv_obj_set_size(settings_Menu, lv_pct(100), lv_pct(100));
    lv_obj_center(settings_Menu);
    lv_obj_set_style_bg_color(settings_Menu, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(settings_Menu, 0, 0);
    lv_menu_set_mode_header(settings_Menu, LV_MENU_HEADER_TOP_FIXED);

    wifi_Page = ui_Settings_Create_WiFi_Page(settings_Menu);
    display_Page = ui_Settings_Create_Display_Page(settings_Menu);
    lepton_Page = ui_Settings_Create_Lepton_Page(settings_Menu);
    image_Page = ui_Settings_Create_Image_Page(settings_Menu);
    calibration_Page = ui_Settings_Create_Calibration_Page(settings_Menu);
    memory_Page = ui_Settings_Create_Memory_Page(settings_Menu);
    usb_Page = ui_Settings_Create_USB_Page(settings_Menu);
    system_Page = ui_Settings_Create_System_Page(settings_Menu);
    about_Page = ui_Settings_Create_About_Page(settings_Menu);

    root_page = lv_menu_page_create(settings_Menu, NULL);
    lv_obj_set_style_bg_color(root_page, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(root_page, 0, 0);

    section = lv_menu_section_create(root_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 0, 0);
    lv_obj_set_style_pad_all(section, 8, 0);

    lv_obj_t *wifi_cont = ui_Settings_Create_Text(section, "WiFi");
    lv_menu_set_load_page_event(settings_Menu, wifi_cont, wifi_Page);
    cont = wifi_cont;

    cont = ui_Settings_Create_Text(section, "Display");
    lv_menu_set_load_page_event(settings_Menu, cont, display_Page);

    cont = ui_Settings_Create_Text(section, "Lepton");
    lv_menu_set_load_page_event(settings_Menu, cont, lepton_Page);

    cont = ui_Settings_Create_Text(section, "Image");
    lv_menu_set_load_page_event(settings_Menu, cont, image_Page);

    cont = ui_Settings_Create_Text(section, "Calibration");
    lv_menu_set_load_page_event(settings_Menu, cont, calibration_Page);

    cont = ui_Settings_Create_Text(section, "Memory");
    lv_menu_set_load_page_event(settings_Menu, cont, memory_Page);
    lv_obj_add_event_cb(cont, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ui_settings_update_memory_usage();
        }
    }, LV_EVENT_CLICKED, NULL);

    cont = ui_Settings_Create_Text(section, "USB");
    lv_menu_set_load_page_event(settings_Menu, cont, usb_Page);

    cont = ui_Settings_Create_Text(section, "System");
    lv_menu_set_load_page_event(settings_Menu, cont, system_Page);

    cont = ui_Settings_Create_Text(section, "About");
    lv_menu_set_load_page_event(settings_Menu, cont, about_Page);

    lv_menu_set_sidebar_page(settings_Menu, root_page);

    /* Pre-select the WiFi page so it is shown and highlighted when the settings menu opens */
    lv_obj_send_event(wifi_cont, LV_EVENT_CLICKED, NULL);

    /* Register event handler for menu page changes to control Save button visibility */
    lv_obj_add_event_cb(settings_Menu, on_Menu_PageChanged, LV_EVENT_VALUE_CHANGED, NULL);

    /* Initially check current page (root page doesn't need Save button hidden) */
    lv_obj_remove_flag(ui_Button_Menu_Button4, LV_OBJ_FLAG_HIDDEN);

    esp_event_handler_register(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_WIFI_CHANGED, on_Settings_Event_Handler, NULL);

    /* Synchronize USB switch state with current cable connection status.
       If the cable was already connected before the settings page was opened,
       the USB_EVENT_CABLE_CONNECTED event was missed - enable switches now. */
    if (USBManager_IsCableConnected()) {
        lv_obj_remove_state(usb_mode_switch, LV_STATE_DISABLED);
        lv_obj_remove_state(usb_uvc_switch, LV_STATE_DISABLED);
    }
}

void ui_settings_deinit(lv_obj_t *p_Parent)
{
    if (ui_Splash) {
        lv_obj_del(settings_Menu);
        lv_obj_delete(settings_Menu);

        esp_event_handler_unregister(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler);
        esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_WIFI_CHANGED, on_Settings_Event_Handler);
    }
}

void ui_settings_update_memory_usage(void)
{
    MemoryManager_Usage_t Usage;

    if (MemoryManager_IsFilesystemLocked()) {
        ESP_LOGD(TAG, "Cannot update memory usage - filesystem is locked");

        return;
    }

    esp_task_wdt_reset();

    if (MemoryManager_GetStorageUsage(&Usage) == ESP_OK) {
        if (Usage.TotalBytes >= (1024 * 1024)) {
            lv_label_set_text_fmt(memory_storage_used_label, "%.2f MB", Usage.UsedBytes / (1024.0f * 1024.0f));
            lv_label_set_text_fmt(memory_storage_free_label, "%.2f MB", Usage.FreeBytes / (1024.0f * 1024.0f));
        } else {
            lv_label_set_text_fmt(memory_storage_used_label, "%.1f KB", Usage.UsedBytes / 1024.0f);
            lv_label_set_text_fmt(memory_storage_free_label, "%.1f KB", Usage.FreeBytes / 1024.0f);
        }

        ESP_LOGD(TAG, "Updated flash storage usage: Used %u bytes, Free %u bytes", Usage.UsedBytes, Usage.FreeBytes);
    }

    esp_task_wdt_reset();

    if (MemoryManager_GetCoredumpUsage(&Usage) == ESP_OK) {
        lv_label_set_text_fmt(memory_coredump_used_label, "%u KB", Usage.UsedBytes / 1024);
        lv_label_set_text_fmt(memory_coredump_total_label, "%u KB", Usage.TotalBytes / 1024);

        ESP_LOGD(TAG, "Updated flash coredump usage: Used %u bytes, Total %u bytes", Usage.UsedBytes, Usage.TotalBytes);
    }
}