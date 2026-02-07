/*
 * ui_Settings.cpp
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

#include <string>
#include <string.h>

#include "../Export/ui.h"

#include "ui_Settings.h"
#include "Private/ui_Settings_Events.h"
#include "../../../Manager/Settings/settingsManager.h"
#include "../../../Manager/Memory/memoryManager.h"
#include "../../../Manager/Network/Server/server.h"

Slider_Widgets_t brightness_widgets;
Slider_Widgets_t emissivity_widgets;

/* Flash page widget references */
static lv_obj_t * flash_storage_used_label = NULL;
static lv_obj_t * flash_storage_free_label = NULL;
static lv_obj_t * flash_coredump_used_label = NULL;
static lv_obj_t * flash_coredump_total_label = NULL;

lv_obj_t * root_page;
lv_obj_t * menu;
lv_obj_t * cont;
lv_obj_t * section;
lv_obj_t * about_Page;
lv_obj_t * wifi_Page;
lv_obj_t * display_Page;
lv_obj_t * lepton_Page;
lv_obj_t * flash_Page;
lv_obj_t * settings_Menu;
lv_obj_t * emissivity_Dropdown;
lv_obj_t * wifi_status_label;

static lv_obj_t * create_menu_container(lv_obj_t * parent) {
    lv_obj_t * container = lv_obj_create(parent);
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

static lv_obj_t * create_row(lv_obj_t * parent, lv_flex_align_t main_align) {
    lv_obj_t * row = lv_obj_create(parent);
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

static lv_obj_t* create_compact_slider(lv_obj_t * parent, const char * label_text, 
                                         int min_val, int max_val, int init_val, Slider_Widgets_t *widgets) {
    char Buffer[8];

    if(widgets == NULL) {
        return NULL;
    }

    lv_obj_t * label_row = create_row(parent, LV_FLEX_ALIGN_START);
    lv_obj_t * label = lv_label_create(label_row);
    lv_label_set_text(label, label_text);
    lv_obj_set_width(label, LV_PCT(50));
    lv_obj_set_style_text_color(label, lv_color_white(), 0);

    snprintf(Buffer, sizeof(Buffer), "%i", init_val);
    lv_obj_t * value_label = lv_label_create(label_row);
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

    lv_obj_t * slider_row = create_row(parent, LV_FLEX_ALIGN_CENTER);
    lv_obj_t * slider = lv_slider_create(slider_row);
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

static Menu_Page_Result_t create_menu_page_with_container(lv_obj_t * menu) {
    lv_obj_t * page = lv_menu_page_create(menu, NULL);
    lv_obj_t * section = lv_menu_section_create(page);
    lv_obj_t * container = create_menu_container(section);

    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scrollbar_mode(section, LV_SCROLLBAR_MODE_AUTO);

    /* Allow scrolling for content overflow */
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scroll_dir(section, LV_DIR_VER);

    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_border_width(section, 0, 0);

    lv_obj_set_style_bg_color(page, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(page, 0, 0);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 0, 0);

    /* Style scrollbar - LVGL 9.x */
    lv_obj_set_style_pad_right(section, 8, LV_PART_SCROLLBAR);

    return {container, page};
}

static lv_obj_t * create_text(lv_obj_t * parent, const char * txt) {
    lv_obj_t * obj = lv_menu_cont_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_EDITED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, 255, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 10, 0);
    lv_obj_set_style_margin_bottom(obj, 4, 0);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_EDITED);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_EDITED);
    lv_obj_set_style_border_width(obj, 15, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF9500), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(obj, 15, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFF9500), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(obj, 15, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFF9500), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(obj, 15, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0xFF9500), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(obj, 180, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(obj, 180, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_opa(obj, 180, LV_STATE_PRESSED);
    lv_obj_set_size(obj, LV_PCT(95), 36);

    lv_obj_t * label = NULL;

    if(txt) {
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
static lv_obj_t * ui_Settings_Create_About_Page(lv_obj_t *p_parent)
{
    lv_obj_t * cont;
    lv_obj_t * section;
    lv_obj_t * about_page;
    lv_obj_t * about_container;
    Menu_Page_Result_t about_result;
    lv_obj_t * sub_software_info_page;
    lv_obj_t * sub_legal_info_page;
    char Buffer[64];

    about_result = create_menu_page_with_container(p_parent);
    about_container = about_result.Container;
    about_page = about_result.Page;

    lv_menu_separator_create(about_page);

    sub_software_info_page = lv_menu_page_create(p_parent, NULL);
    section = lv_menu_section_create(sub_software_info_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 255, 0);
    lv_obj_set_style_pad_all(section, 8, 0);
    create_text(section, "PyroVision Firmware");

    memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Version %u.%u.%u", 1, 0, 0);
    create_text(section, Buffer);

    memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Platform: %s", CONFIG_IDF_TARGET);
    create_text(section, Buffer);

    memset(Buffer, 0, sizeof(Buffer));
    snprintf(Buffer, sizeof(Buffer), "LVGL Version: %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    create_text(section, Buffer);

    sub_legal_info_page = lv_menu_page_create(p_parent, NULL);
    section = lv_menu_section_create(sub_legal_info_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 255, 0);
    lv_obj_set_style_pad_all(section, 8, 0);

    lv_obj_t * license_label = lv_label_create(section);
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

    cont = create_text(about_container, "Software information");
    lv_menu_set_load_page_event(p_parent, cont, sub_software_info_page);
    cont = create_text(about_container, "Legal information");
    lv_menu_set_load_page_event(p_parent, cont, sub_legal_info_page);

    return about_page;
}

/** @brief          Creates the WiFi settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t* ui_Settings_Create_WiFi_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t WiFiResult = create_menu_page_with_container(p_Menu);
    lv_obj_t * WiFiContainer = WiFiResult.Container;
    lv_obj_t * WiFiPage = WiFiResult.Page;
    App_Settings_WiFi_t WiFiSettings;

    SettingsManager_GetWiFi(&WiFiSettings);

    lv_obj_t * wifi_row1 = create_row(WiFiContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * wifi_label = lv_label_create(wifi_row1);
    lv_label_set_text(wifi_label, "Autoconnect");
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);

    lv_obj_t * wifi_switch = lv_switch_create(wifi_row1);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0x7B3FF0), 0);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0xFF9500), LV_PART_KNOB);
    lv_obj_add_event_cb(wifi_switch, on_WiFi_Autoconnect_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    if(WiFiSettings.AutoConnect) {
        lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(wifi_switch, LV_STATE_CHECKED);
    }

    lv_obj_t * wifi_row2 = create_row(WiFiContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * ssid_label = lv_label_create(wifi_row2);
    lv_label_set_text(ssid_label, "Status");
    lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
    wifi_status_label = lv_label_create(wifi_row2);
    lv_label_set_text(wifi_status_label, "Offline");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFF9500), 0);

    /* WiFi Connect Button */
    lv_obj_t * wifi_btn_row = create_row(WiFiContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(wifi_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(wifi_btn_row, 12, 0);
    lv_obj_set_style_pad_bottom(wifi_btn_row, 8, 0);
    lv_obj_t * wifi_connect_btn = lv_btn_create(wifi_btn_row);
    lv_obj_set_size(wifi_connect_btn, 140, 36);
    lv_obj_set_style_bg_color(wifi_connect_btn, lv_color_hex(0xFF9500), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(wifi_connect_btn, 6, 0);
    lv_obj_set_style_shadow_width(wifi_connect_btn, 0, 0);
    lv_obj_add_event_cb(wifi_connect_btn, on_WiFi_Connect_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t * wifi_btn_label = lv_label_create(wifi_connect_btn);
    lv_label_set_text(wifi_btn_label, "Connect WiFi");
    lv_obj_set_style_text_color(wifi_btn_label, lv_color_white(), 0);
    lv_obj_center(wifi_btn_label);

    return WiFiPage;
}

/** @brief          Creates the display settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t* ui_Settings_Create_Display_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t DisplayResult = create_menu_page_with_container(p_Menu);
    lv_obj_t * DisplayContainer = DisplayResult.Container;
    lv_obj_t * DisplayPage = DisplayResult.Page;
    App_Settings_Display_t DisplaySettings;

    SettingsManager_GetDisplay(&DisplaySettings);

    lv_obj_t *brightness_slider = create_compact_slider(DisplayContainer, "Brightness", 0, 100, DisplaySettings.Brightness, &brightness_widgets);

    lv_obj_add_event_cb(brightness_slider, on_Display_Brightness_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);

    return DisplayPage;
}

/** @brief          Creates the Lepton settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t* ui_Settings_Create_Lepton_Page(lv_obj_t *p_Menu)
{
    std::string Buffer;
    Menu_Page_Result_t LeptonResult = create_menu_page_with_container(p_Menu);
    lv_obj_t * LeptonContainer = LeptonResult.Container;
    lv_obj_t * LeptonPage = LeptonResult.Page;
    App_Settings_Lepton_t LeptonSettings;

    SettingsManager_GetLepton(&LeptonSettings);

    for(size_t i = 0; i < LeptonSettings.EmissivityCount; i++) {
        Buffer += LeptonSettings.EmissivityPresets[i].Description + std::string("\n");
    }

    lv_obj_t *emissivity_slider = create_compact_slider(LeptonContainer, "Emissivity", 0, 100, LeptonSettings.CurrentEmissivity, &emissivity_widgets);

    lv_obj_add_event_cb(emissivity_slider, on_Lepton_Emissivity_Slider_Callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(emissivity_slider, on_Lepton_Emissivity_Slider_Callback, LV_EVENT_RELEASED, NULL);

    lv_obj_t * dropdown_row = create_row(LeptonContainer, LV_FLEX_ALIGN_CENTER);
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

    return LeptonPage;
}

/** @brief          Creates the Flash settings page and load it with the values from the settings.
 *  @param p_Menu   Pointer to the menu object
 */
static lv_obj_t* ui_Settings_Create_Flash_Page(lv_obj_t *p_Menu)
{
    Menu_Page_Result_t FlashResult = create_menu_page_with_container(p_Menu);
    lv_obj_t * FlashContainer = FlashResult.Container;
    lv_obj_t * FlashPage = FlashResult.Page;

    /* === NVS Settings Section === */
    lv_obj_t * nvs_section_label = lv_label_create(FlashContainer);
    lv_label_set_text(nvs_section_label, "NVS Settings");
    lv_obj_set_style_text_color(nvs_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(nvs_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(nvs_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(nvs_section_label, 8, 0);

    lv_obj_t * nvs_desc_label = lv_label_create(FlashContainer);
    lv_label_set_text(nvs_desc_label, "Reset all settings to factory defaults");
    lv_obj_set_style_text_color(nvs_desc_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(nvs_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(nvs_desc_label, 8, 0);

    lv_obj_t * nvs_btn_row = create_row(FlashContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(nvs_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(nvs_btn_row, 4, 0);
    lv_obj_set_style_pad_bottom(nvs_btn_row, 4, 0);
    lv_obj_t * nvs_clear_btn = lv_btn_create(nvs_btn_row);
    lv_obj_set_size(nvs_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(nvs_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(nvs_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(nvs_clear_btn, 0, 0);
    lv_obj_add_event_cb(nvs_clear_btn, on_Flash_ClearNVS_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t * nvs_btn_label = lv_label_create(nvs_clear_btn);
    lv_label_set_text(nvs_btn_label, "Reset");
    lv_obj_set_style_text_color(nvs_btn_label, lv_color_white(), 0);
    lv_obj_center(nvs_btn_label);

    /* Separator */
    lv_obj_t * separator1 = lv_obj_create(FlashContainer);
    lv_obj_set_size(separator1, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator1, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator1, 0, 0);
    lv_obj_set_style_pad_all(separator1, 0, 0);
    lv_obj_set_style_margin_top(separator1, 12, 0);
    lv_obj_set_style_margin_bottom(separator1, 12, 0);

    /* === Storage Partition Section === */
    lv_obj_t * storage_section_label = lv_label_create(FlashContainer);
    lv_label_set_text(storage_section_label, "Storage Partition");
    lv_obj_set_style_text_color(storage_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(storage_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(storage_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(storage_section_label, 8, 0);

    lv_obj_t * storage_info_row = create_row(FlashContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * storage_used_label = lv_label_create(storage_info_row);
    lv_label_set_text(storage_used_label, "Used:");
    lv_obj_set_style_text_color(storage_used_label, lv_color_white(), 0);
    lv_obj_t * storage_used_value = lv_label_create(storage_info_row);
    lv_label_set_text(storage_used_value, "-- KB");
    lv_obj_set_style_text_color(storage_used_value, lv_color_hex(0xB998FF), 0);

    lv_obj_t * storage_free_row = create_row(FlashContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * storage_free_label = lv_label_create(storage_free_row);
    lv_label_set_text(storage_free_label, "Free:");
    lv_obj_set_style_text_color(storage_free_label, lv_color_white(), 0);
    lv_obj_t * storage_free_value = lv_label_create(storage_free_row);
    lv_label_set_text(storage_free_value, "-- KB");
    lv_obj_set_style_text_color(storage_free_value, lv_color_hex(0xB998FF), 0);

    lv_obj_t * storage_btn_row = create_row(FlashContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(storage_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(storage_btn_row, 8, 0);
    lv_obj_set_style_pad_bottom(storage_btn_row, 8, 0);
    lv_obj_t * storage_clear_btn = lv_btn_create(storage_btn_row);
    lv_obj_set_size(storage_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(storage_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(storage_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(storage_clear_btn, 0, 0);
    lv_obj_add_event_cb(storage_clear_btn, on_Flash_ClearStorage_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t * storage_btn_label = lv_label_create(storage_clear_btn);
    lv_label_set_text(storage_btn_label, "Clear");
    lv_obj_set_style_text_color(storage_btn_label, lv_color_white(), 0);
    lv_obj_center(storage_btn_label);

    /* Separator */
    lv_obj_t * separator2 = lv_obj_create(FlashContainer);
    lv_obj_set_size(separator2, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(separator2, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(separator2, 0, 0);
    lv_obj_set_style_pad_all(separator2, 0, 0);
    lv_obj_set_style_margin_top(separator2, 12, 0);
    lv_obj_set_style_margin_bottom(separator2, 12, 0);

    /* === Coredump Partition Section === */
    lv_obj_t * coredump_section_label = lv_label_create(FlashContainer);
    lv_label_set_text(coredump_section_label, "Coredump Partition");
    lv_obj_set_style_text_color(coredump_section_label, lv_color_hex(0xFF9500), 0);
    lv_obj_set_style_text_font(coredump_section_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(coredump_section_label, 0, 0);
    lv_obj_set_style_pad_bottom(coredump_section_label, 8, 0);

    lv_obj_t * coredump_info_row = create_row(FlashContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * coredump_used_label = lv_label_create(coredump_info_row);
    lv_label_set_text(coredump_used_label, "Used:");
    lv_obj_set_style_text_color(coredump_used_label, lv_color_white(), 0);
    lv_obj_t * coredump_used_value = lv_label_create(coredump_info_row);
    lv_label_set_text(coredump_used_value, "-- KB");
    lv_obj_set_style_text_color(coredump_used_value, lv_color_hex(0xB998FF), 0);

    lv_obj_t * coredump_total_row = create_row(FlashContainer, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_t * coredump_total_label = lv_label_create(coredump_total_row);
    lv_label_set_text(coredump_total_label, "Total:");
    lv_obj_set_style_text_color(coredump_total_label, lv_color_white(), 0);
    lv_obj_t * coredump_total_value = lv_label_create(coredump_total_row);
    lv_label_set_text(coredump_total_value, "-- KB");
    lv_obj_set_style_text_color(coredump_total_value, lv_color_hex(0xB998FF), 0);

    lv_obj_t * coredump_btn_row = create_row(FlashContainer, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(coredump_btn_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(coredump_btn_row, 8, 0);
    lv_obj_set_style_pad_bottom(coredump_btn_row, 16, 0);
    lv_obj_t * coredump_clear_btn = lv_btn_create(coredump_btn_row);
    lv_obj_set_size(coredump_clear_btn, 120, 36);
    lv_obj_set_style_bg_color(coredump_clear_btn, lv_color_hex(0xFF3B3B), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(coredump_clear_btn, 6, 0);
    lv_obj_set_style_shadow_width(coredump_clear_btn, 0, 0);
    lv_obj_add_event_cb(coredump_clear_btn, on_Flash_ClearCoredump_Callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t * coredump_btn_label = lv_label_create(coredump_clear_btn);
    lv_label_set_text(coredump_btn_label, "Clear");
    lv_obj_set_style_text_color(coredump_btn_label, lv_color_white(), 0);
    lv_obj_center(coredump_btn_label);

    flash_storage_used_label = storage_used_value;
    flash_storage_free_label = storage_free_value;
    flash_coredump_used_label = coredump_used_value;
    flash_coredump_total_label = coredump_total_value;

    return FlashPage;
}

void ui_settings_build(lv_obj_t *p_Parent)
{
    menu = lv_menu_create(p_Parent);
    settings_Menu = menu;
    lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
    lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(menu, 0, 0);

    lv_menu_set_mode_header(menu, LV_MENU_HEADER_TOP_FIXED);

    wifi_Page = ui_Settings_Create_WiFi_Page(menu);
    display_Page = ui_Settings_Create_Display_Page(menu);
    lepton_Page = ui_Settings_Create_Lepton_Page(menu);
    flash_Page = ui_Settings_Create_Flash_Page(menu);
    about_Page = ui_Settings_Create_About_Page(menu);

    root_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_bg_color(root_page, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(root_page, 0, 0);

    section = lv_menu_section_create(root_page);
    lv_obj_set_style_bg_color(section, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(section, 0, 0);
    lv_obj_set_style_pad_all(section, 8, 0);

    cont = create_text(section, "WiFi");
    lv_menu_set_load_page_event(menu, cont, wifi_Page);

    cont = create_text(section, "Display");
    lv_menu_set_load_page_event(menu, cont, display_Page);

    cont = create_text(section, "Lepton");
    lv_menu_set_load_page_event(menu, cont, lepton_Page);

    cont = create_text(section, "Flash");
    lv_menu_set_load_page_event(menu, cont, flash_Page);
    lv_obj_add_event_cb(cont, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            ui_settings_update_flash_usage();
        }
    }, LV_EVENT_CLICKED, NULL);

    cont = create_text(section, "About");
    lv_menu_set_load_page_event(menu, cont, about_Page);

    lv_menu_set_sidebar_page(menu, root_page);

    esp_event_handler_register(SETTINGS_EVENTS, ESP_EVENT_ANY_ID, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
}

void ui_settings_deinit(lv_obj_t *p_Parent)
{
    if(ui_Splash) {
        lv_obj_del(settings_Menu);
        lv_obj_delete(settings_Menu);

        esp_event_handler_unregister(SETTINGS_EVENTS, ESP_EVENT_ANY_ID, on_Settings_Event_Handler);
        esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    }
}

void ui_settings_update_flash_usage(void)
{
    MemoryManager_Usage_t Usage;

    if ((flash_storage_used_label != NULL) && (flash_storage_free_label != NULL)) {
        esp_err_t Error = MemoryManager_GetStorageUsage(&Usage);
        if (Error == ESP_OK) {
            if (Usage.TotalBytes >= 1024 * 1024) {
                lv_label_set_text_fmt(flash_storage_used_label, "%.2f MB", Usage.UsedBytes / (1024.0f * 1024.0f));
                lv_label_set_text_fmt(flash_storage_free_label, "%.2f MB", Usage.FreeBytes / (1024.0f * 1024.0f));
            } else {
                lv_label_set_text_fmt(flash_storage_used_label, "%.1f KB", Usage.UsedBytes / 1024.0f);
                lv_label_set_text_fmt(flash_storage_free_label, "%.1f KB", Usage.FreeBytes / 1024.0f);
            }
        } else {
            lv_label_set_text(flash_storage_used_label, "Error");
            lv_label_set_text(flash_storage_free_label, "Error");
        }
    }

    if ((flash_coredump_used_label != NULL) && (flash_coredump_total_label != NULL)) {
        esp_err_t Error = MemoryManager_GetCoredumpUsage(&Usage);
        if (Error == ESP_OK) {
            lv_label_set_text_fmt(flash_coredump_used_label, "%u KB", Usage.UsedBytes / 1024);
            lv_label_set_text_fmt(flash_coredump_total_label, "%u KB", Usage.TotalBytes / 1024);
        } else {
            lv_label_set_text(flash_coredump_used_label, "Error");
            lv_label_set_text(flash_coredump_total_label, "Error");
        }
    }
}