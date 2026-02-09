/*
 * guiTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task implementation.
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

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include <esp_efuse.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lvgl.h>

#include <cstring>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "guiTask.h"
#include "Export/ui.h"
#include "Application/application.h"
#include "Application/Manager/managers.h"
#include "Application/Manager/Network/Server/server.h"
#include "Private/guiHelper.h"

#include "lepton.h"

ESP_EVENT_DEFINE_BASE(GUI_EVENTS);

static GUI_Task_State_t _GUITask_State;
static QueueHandle_t _ImageSaveQueue = NULL;
static TaskHandle_t _ImageSaveTaskHandle = NULL;

static const char *TAG = "gui_task";

/* Forward declaration of background image save task */
static void Task_ImageSave(void *p_Param);

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Devices_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Devices event received: ID=%d", ID);

    switch (ID) {
        case DEVICE_EVENT_RESPONSE_BATTERY_VOLTAGE: {
            memcpy(&_GUITask_State.BatteryInfo, p_Data, sizeof(App_Devices_Battery_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, BATTERY_VOLTAGE_READY);

            break;
        }
        case DEVICE_EVENT_RESPONSE_CHARGING: {
            xEventGroupSetBits(_GUITask_State.EventGroup, BATTERY_CHARGING_STATUS_READY);

            break;
        }
    }
}

/** @brief              Time event handler.
 *  @param p_HandlerArgs Handler argument
 *  @param Base         Event base
 *  @param ID           Event ID
 *  @param p_Data       Event-specific data
 */
static void on_Time_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Time event received: ID=%d", ID);

    switch (ID) {
        case TIME_EVENT_SYNCHRONIZED: {
            break;
        }
        case TIME_EVENT_SOURCE_CHANGED: {
            break;
        }
    }
}

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Network event received: ID=%d", ID);

    switch (ID) {
        case NETWORK_EVENT_WIFI_CONNECTED: {
            break;
        }
        case NETWORK_EVENT_WIFI_GOT_IP: {
            memcpy(&_GUITask_State.IP_Info, p_Data, sizeof(Network_IP_Info_t));
            _GUITask_State.WiFiConnected = true;

            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STARTED: {
            _GUITask_State.ProvisioningActive = true;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STOPPED: {
            _GUITask_State.ProvisioningActive = false;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            /* Also trigger WiFi status update to show correct connection state */
            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            _GUITask_State.ProvisioningActive = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            _GUITask_State.ProvisioningActive = false;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            /* Also trigger WiFi status update to show correct connection state */
            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_SERVER_STARTED: {
            /* Register thermal frame with server (called after server is started) */
            Server_SetThermalFrame(&_GUITask_State.NetworkFrame);
            ESP_LOGD(TAG, "Network frame registered with server");

            break;
        }
        case NETWORK_EVENT_AP_STA_CONNECTED: {
            break;
        }
        case NETWORK_EVENT_AP_STA_DISCONNECTED: {
            break;
        }
    }
}

/** @brief                  SD card event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_SD_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "SD card event received: ID=%ld", ID);

    switch (ID) {
        default: {
            ESP_LOGW(TAG, "Unknown SD event ID: %ld", ID);
            break;
        }
    }
}

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Lepton_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Lepton event received: ID=%d", ID);

    switch (ID) {
        case LEPTON_EVENT_CAMERA_READY: {
            memcpy(&_GUITask_State.LeptonDeviceInfo, ((App_Lepton_Device_t *)p_Data), sizeof(App_Lepton_Device_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_CAMERA_READY);

            break;
        }
        case LEPTON_EVENT_CAMERA_ERROR: {
            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_CAMERA_ERROR);

            break;
        }
        case LEPTON_EVENT_RESPONSE_FPA_AUX_TEMP: {
            memcpy(&_GUITask_State.LeptonTemperatures, p_Data, sizeof(App_Lepton_Temperatures_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_TEMP_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_SPOTMETER: {
            memcpy(&_GUITask_State.ROIResult, p_Data, sizeof(App_Lepton_ROI_Result_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_SPOTMETER_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_SCENE_STATISTICS: {
            memcpy(&_GUITask_State.ROIResult, p_Data, sizeof(App_Lepton_ROI_Result_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_SCENE_STATISTICS_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_UPTIME: {
            memcpy(&_GUITask_State.LeptonUptime, p_Data, sizeof(uint32_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_UPTIME_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_PIXEL_TEMPERATURE: {
            memcpy(&_GUITask_State.SpotTemperature, p_Data, sizeof(float));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_PIXEL_TEMPERATURE_READY);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled Lepton event ID: %d", ID);
            break;
        }
    }
}

/** @brief Update the information screen labels.
 */
static void GUI_Update_Info(void)
{
    uint8_t mac[6];
    char mac_str[19];

    esp_efuse_mac_get_default(mac);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    lv_label_set_text(ui_Label_Info_Lepton_Serial, _GUITask_State.LeptonDeviceInfo.SerialNumber);
    lv_label_set_text(ui_Label_Info_Lepton_Part, _GUITask_State.LeptonDeviceInfo.PartNumber);
    lv_label_set_text(ui_Label_Info_Lepton_GPP_Revision, _GUITask_State.LeptonDeviceInfo.SoftwareRevision.GPP_Revision);
    lv_label_set_text(ui_Label_Info_Lepton_DSP_Revision, _GUITask_State.LeptonDeviceInfo.SoftwareRevision.DSP_Revision);
    lv_label_set_text(ui_Label_Info_MAC, mac_str);
}

/** @brief      Update the ROI rectangle on the GUI and on the Lepton.
 *  @param Type ROI type
 *  @param x    X position of the ROI (in Lepton coordinates 0-159)
 *  @param y    Y position (in Lepton coordinates 0-119)
 *  @param w    Width of the ROI
 *  @param h    Height of the ROI
 */
static void GUI_Update_ROI(App_Settings_ROI_t ROI)
{
    int32_t DisplayWidth;
    int32_t DisplayHeight;
    App_Settings_Lepton_t SettingsLepton;

    if ((ROI.x + ROI.w) > 160) {
        ROI.w = 160 - ROI.x;
    }

    if ((ROI.y + ROI.h) > 120) {
        ROI.h = 120 - ROI.y;
    }

    /* Minimum size constraints */
    if (ROI.w < 1) {
        ROI.w = 1;
    } else if (ROI.h < 1) {
        ROI.h = 1;
    }

    /* Update visual rectangle on display (convert Lepton coords to display coords) */
    ESP_LOGD(TAG, "Updating ROI rectangle - Start: (%ld,%ld), End: (%ld,%ld), Size: %ldx%ld",
             ROI.x, ROI.y, ROI.x + ROI.w, ROI.y + ROI.h, ROI.w, ROI.h);

    DisplayWidth = lv_obj_get_width(ui_Image_Thermal);
    DisplayHeight = lv_obj_get_height(ui_Image_Thermal);

    int32_t disp_x = (ROI.x * DisplayWidth) / 160;
    int32_t disp_y = (ROI.y * DisplayHeight) / 120;
    int32_t disp_w = (ROI.w * DisplayWidth) / 160;
    int32_t disp_h = (ROI.h * DisplayHeight) / 120;

    switch (ROI.Type) {
        case ROI_TYPE_SPOTMETER: {
            if (ui_Image_Main_Thermal_Spotmeter_ROI != NULL) {
                lv_obj_set_align(ui_Image_Main_Thermal_Spotmeter_ROI, LV_ALIGN_TOP_LEFT);
                lv_obj_set_pos(ui_Image_Main_Thermal_Spotmeter_ROI, disp_x, disp_y);
                lv_obj_set_size(ui_Image_Main_Thermal_Spotmeter_ROI, disp_w, disp_h);
            }

            break;
        }
        case ROI_TYPE_SCENE: {
            if (ui_Image_Main_Thermal_Scene_ROI != NULL) {
                lv_obj_set_align(ui_Image_Main_Thermal_Scene_ROI, LV_ALIGN_TOP_LEFT);
                lv_obj_set_pos(ui_Image_Main_Thermal_Scene_ROI, disp_x, disp_y);
                lv_obj_set_size(ui_Image_Main_Thermal_Scene_ROI, disp_w, disp_h);
            }

            break;
        }
        case ROI_TYPE_AGC: {
            if (ui_Image_Main_Thermal_AGC_ROI != NULL) {
                lv_obj_set_align(ui_Image_Main_Thermal_AGC_ROI, LV_ALIGN_TOP_LEFT);
                lv_obj_set_pos(ui_Image_Main_Thermal_AGC_ROI, disp_x, disp_y);
                lv_obj_set_size(ui_Image_Main_Thermal_AGC_ROI, disp_w, disp_h);
            }

            break;
        }
        case ROI_TYPE_VIDEO_FOCUS: {
            if (ui_Image_Main_Thermal_Video_Focus_ROI != NULL) {
                lv_obj_set_align(ui_Image_Main_Thermal_Video_Focus_ROI, LV_ALIGN_TOP_LEFT);
                lv_obj_set_pos(ui_Image_Main_Thermal_Video_Focus_ROI, disp_x, disp_y);
                lv_obj_set_size(ui_Image_Main_Thermal_Video_Focus_ROI, disp_w, disp_h);
            }

            break;
        }
        default: {
            ESP_LOGW(TAG, "Invalid GUI ROI type: %d", ROI.Type);
            return;
        }
    }

    SettingsManager_GetLepton(&SettingsLepton);

    /* Check if an update is required */
    if ((SettingsLepton.ROI[ROI.Type].x == ROI.x) &&
        (SettingsLepton.ROI[ROI.Type].y == ROI.y) &&
        (SettingsLepton.ROI[ROI.Type].w == ROI.w) &&
        (SettingsLepton.ROI[ROI.Type].h == ROI.h)) {
        ESP_LOGW(TAG, "ROI unchanged, not updating NVS");
        return;
    }

    /* Copy the new ROI in the existing settings structure */
    memcpy(&SettingsLepton.ROI[ROI.Type], &ROI, sizeof(App_Settings_ROI_t));

    /* Save the new ROI to NVS */
    SettingsManager_UpdateLepton(&SettingsLepton);
    SettingsManager_Save();

    /* The Lepton task needs the ROI with the Lepton coordinates */
    SettingsLepton.ROI[ROI.Type] = {
        .Type = ROI.Type,
        .x = (uint16_t)ROI.x,
        .y = (uint16_t)ROI.y,
        .w = (uint16_t)ROI.w,
        .h = (uint16_t)ROI.h
    };

    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_ROI, &SettingsLepton.ROI[ROI.Type], sizeof(App_Settings_ROI_t),
                   portMAX_DELAY);
}

/** @brief Create temperature gradient canvas for palette visualization.
 *         Generates a vertical gradient from hot (top) to cold (bottom).
 */
static void UI_Canvas_AddTempGradient(void)
{
    /* Generate gradient pixel by pixel */
    uint16_t *buffer = (uint16_t *)_GUITask_State.GradientCanvasBuffer;

    for (uint32_t y = 0; y < _GUITask_State.GradientImageDescriptor.header.h; y++) {
        uint32_t index;

        /* Map y position to palette index (0 = top/hot = white, 179 = bottom/cold = black)
         * Iron palette: index 0 = black (cold), index 255 = white (hot)
         * So we need to invert: top (y=0) should be index 255, bottom (y=179) should be index 0
         */
        index = 255 - (y * 255 / (_GUITask_State.GradientImageDescriptor.header.h - 1));

        /* Get RGB888 values from palette */
        uint8_t r8 = Lepton_Palette_Iron[index][0];
        uint8_t g8 = Lepton_Palette_Iron[index][1];
        uint8_t b8 = Lepton_Palette_Iron[index][2];

        /* Convert RGB888 to RGB565 */
        uint16_t r5 = (r8 >> 3) & 0x1F;
        uint16_t g6 = (g8 >> 2) & 0x3F;
        uint16_t b5 = (b8 >> 3) & 0x1F;

        /* Fill entire row with same color */
        for (uint32_t x = 0; x < _GUITask_State.GradientImageDescriptor.header.w; x++) {
            buffer[y * _GUITask_State.GradientImageDescriptor.header.w + x] = (r5 << 11) | (g6 << 5) | b5;
        }
    }
}

/** @brief          LVGL touch read callback.
 *  @param p_Indev  Input device handle
 *  @param p_Data   Input device data
 */
static void Touch_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    uint8_t Count = 0;
    esp_lcd_touch_point_data_t Data[1];
    esp_lcd_touch_handle_t Touch;

    Touch = (esp_lcd_touch_handle_t)lv_indev_get_user_data(p_Indev);

    /* Check if touch controller is available */
    if (Touch == NULL) {
        p_Data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if ((esp_lcd_touch_read_data(Touch) == ESP_OK) &&
        (esp_lcd_touch_get_data(Touch, Data, &Count, sizeof(Data) / sizeof(Data[0])) == ESP_OK) && (Count > 0)) {
        p_Data->point.x = CONFIG_GUI_WIDTH - Data[0].x;
        p_Data->point.y = Data[0].y;

        p_Data->state = LV_INDEV_STATE_PRESSED;

        ESP_LOGD(TAG, "Raw: (%d, %d)", Data[0].x, Data[0].y);
        ESP_LOGD(TAG, "Mapped: (%d, %d)", p_Data->point.x, p_Data->point.y);

#ifdef CONFIG_GUI_TOUCH_DEBUG
        /* Update touch debug visualization */
        if (_GUITask_State.TouchDebugCircle != NULL) {
            lv_obj_set_pos(_GUITask_State.TouchDebugCircle, p_Data->point.x - 10, p_Data->point.y - 10);
            lv_obj_clear_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_HIDDEN);
        }

        if (_GUITask_State.TouchDebugLabel != NULL) {
            char buf[64];

            snprintf(buf, sizeof(buf), "Raw: %u,%u\nMap: %li,%li", Data[0].x, Data[0].y, p_Data->point.x, p_Data->point.y);
            lv_label_set_text(_GUITask_State.TouchDebugLabel, buf);
            lv_obj_clear_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    } else {
        p_Data->state = LV_INDEV_STATE_RELEASED;

#ifdef CONFIG_GUI_TOUCH_DEBUG
        /* Hide touch debug visualization when released */
        if (_GUITask_State.TouchDebugCircle != NULL) {
            lv_obj_add_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_HIDDEN);
        }

        if (_GUITask_State.TouchDebugLabel != NULL) {
            lv_obj_add_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    }
}

/** @brief              GUI Task main function.
 *  @param p_Parameters Task parameters
 */
void Task_GUI(void *p_Parameters)
{
    App_Context_t *App_Context;
    App_Settings_Lepton_t LeptonSettings;

    esp_task_wdt_add(NULL);

    App_Context = reinterpret_cast<App_Context_t *>(p_Parameters);
    _GUITask_State.p_AppContext = App_Context;  /* Store for later use */
    ESP_LOGD(TAG, "GUI Task started on core %d", xPortGetCoreID());

    /* Show splash screen first and wait for all components to become ready before starting the application. */
    /* Initialization process: */
    /*  - Loading the settings */
    /*  - Waiting for the Lepton */
    do {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_GUITask_State.EventGroup);
        if (EventBits & LEPTON_CAMERA_READY) {

            lv_bar_set_value(ui_SplashScreen_LoadingBar, 100, LV_ANIM_OFF);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_CAMERA_READY);
        } else if (EventBits & LEPTON_CAMERA_ERROR) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Camera Error");

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_CAMERA_ERROR);
        }

        // TODO: Add timeout

        lv_timer_handler();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } while (lv_bar_get_value(ui_SplashScreen_LoadingBar) < lv_bar_get_max_value(ui_SplashScreen_LoadingBar));

    lv_disp_load_scr(ui_Main);

    /* Process layout changes after loading new screen */
    lv_timer_handler();

    /* Set the initial ROI FIRST to give it a size */
    SettingsManager_GetLepton(&LeptonSettings);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SPOTMETER]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SCENE]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_AGC]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_VIDEO_FOCUS]);

    GUI_Update_Info();

    esp_event_post(GUI_EVENTS, GUI_EVENT_APP_STARTED, NULL, 0, portMAX_DELAY);

    while (_GUITask_State.isRunning) {
        EventBits_t EventBits;
        App_Lepton_FrameReady_t LeptonFrame;

        esp_task_wdt_reset();

        /* Check for new thermal frame */
        if (xQueueReceive(App_Context->Lepton_FrameEventQueue, &LeptonFrame, 0) == pdTRUE) {
            uint8_t *Dst;
            uint32_t Image_Width;
            uint32_t Image_Height;
            char Buffer[16];

            /* Variables for Illuminance values for the scene label.
             * Fetch all these values at the beginning to not waste CPU performance because these
             * functions aren´t simple get functions
             *      0 = Max label
             *      1 = Min label
             *      2 = Mean label
             */
            int32_t SceneLabel_x0[3] = { lv_obj_get_x(ui_Label_Main_Thermal_Scene_Max),
                                         lv_obj_get_x(ui_Label_Main_Thermal_Scene_Min),
                                         lv_obj_get_x(ui_Label_Main_Thermal_Scene_Mean)
                                       };
            int32_t SceneLabel_y0[3] = { lv_obj_get_y(ui_Label_Main_Thermal_Scene_Max),
                                         lv_obj_get_y(ui_Label_Main_Thermal_Scene_Min),
                                         lv_obj_get_y(ui_Label_Main_Thermal_Scene_Mean)
                                       };
            int32_t SceneLabel_w[3] = { lv_obj_get_width(ui_Label_Main_Thermal_Scene_Max),
                                        lv_obj_get_width(ui_Label_Main_Thermal_Scene_Min),
                                        lv_obj_get_width(ui_Label_Main_Thermal_Scene_Mean)
                                      };
            int32_t SceneLabel_h[3] = { lv_obj_get_height(ui_Label_Main_Thermal_Scene_Max),
                                        lv_obj_get_height(ui_Label_Main_Thermal_Scene_Min),
                                        lv_obj_get_height(ui_Label_Main_Thermal_Scene_Mean)
                                      };
            uint32_t SceneLabelIlluminance[3] = {0, 0, 0};
            uint32_t SceneLabelCount[3] = {0, 0, 0};
            uint8_t SceneLabelAverageLuminance[3] = {0, 0, 0};

            /* Reset watchdog before image processing */
            esp_task_wdt_reset();

            /* Scale from source (160x120) to destination (240x180) using bilinear interpolation */
            Dst = _GUITask_State.ThermalCanvasBuffer;
            Image_Width = lv_obj_get_width(ui_Image_Thermal);
            Image_Height = lv_obj_get_height(ui_Image_Thermal);

            /* Skip if image widget not properly initialized yet */
            if ((Image_Width == 0) || (Image_Height == 0)) {
                ESP_LOGW(TAG, "Image widget not ready yet (size: %ux%u), skipping frame", Image_Width, Image_Height);
                continue;
            }

            for (uint32_t y = 0; y < Image_Height; y++) {
                uint32_t src_y_fixed = y * ((LeptonFrame.Height - 1) << 16) / Image_Height;
                uint32_t y0 = src_y_fixed >> 16;
                uint32_t y1 = ((y0 + 1) < LeptonFrame.Height) ? (y0 + 1) : y0;
                uint32_t y_frac = (src_y_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                uint32_t y_inv = 256 - y_frac;

                for (uint32_t x = 0; x < Image_Width; x++) {
                    uint32_t src_x_fixed = x * ((LeptonFrame.Width - 1) << 16) / Image_Width;
                    uint32_t x0 = src_x_fixed >> 16;
                    uint32_t x1 = ((x0 + 1) < LeptonFrame.Width) ? (x0 + 1) : x0;
                    uint32_t x_frac = (src_x_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                    uint32_t x_inv = 256 - x_frac;

                    /* Get the four surrounding pixels */
                    uint32_t idx00 = ((y0 * LeptonFrame.Width) + x0) * 3;
                    uint32_t idx10 = ((y0 * LeptonFrame.Width) + x1) * 3;
                    uint32_t idx01 = ((y1 * LeptonFrame.Width) + x0) * 3;
                    uint32_t idx11 = ((y1 * LeptonFrame.Width) + x1) * 3;

                    /* Bilinear interpolation using fixed-point arithmetic (8.8 format) */
                    /* Weight: (256 - x_frac) * (256 - y_frac), x_frac*(256 - y_frac), etc. */
                    uint32_t w00 = (x_inv * y_inv) >> 8;
                    uint32_t w10 = (x_frac * y_inv) >> 8;
                    uint32_t w01 = (x_inv * y_frac) >> 8;
                    uint32_t w11 = (x_frac * y_frac) >> 8;

                    uint32_t r = (LeptonFrame.Buffer[idx00 + 0] * w00 +
                                  LeptonFrame.Buffer[idx10 + 0] * w10 +
                                  LeptonFrame.Buffer[idx01 + 0] * w01 +
                                  LeptonFrame.Buffer[idx11 + 0] * w11) >> 8;

                    uint32_t g = (LeptonFrame.Buffer[idx00 + 1] * w00 +
                                  LeptonFrame.Buffer[idx10 + 1] * w10 +
                                  LeptonFrame.Buffer[idx01 + 1] * w01 +
                                  LeptonFrame.Buffer[idx11 + 1] * w11) >> 8;

                    uint32_t b = (LeptonFrame.Buffer[idx00 + 2] * w00 +
                                  LeptonFrame.Buffer[idx10 + 2] * w10 +
                                  LeptonFrame.Buffer[idx01 + 2] * w01 +
                                  LeptonFrame.Buffer[idx11 + 2] * w11) >> 8;

                    /* Inside the image area under the label: Add the Luminance
                     * Note: The image widget has 180° rotation applied via lv_image_set_rotation.
                     * - x_rot = Image_Width - 1 - x (X axis is inverted due to rotation)
                     * - y is used directly (Y axis matches label position directly)
                     */
                    uint32_t x_rot = Image_Width - 1 - x;

                    /* Max Label */
                    if ((x_rot >= SceneLabel_x0[0]) &&
                        (x_rot < (SceneLabel_x0[0] + SceneLabel_w[0])) &&
                        (y >= SceneLabel_y0[0]) &&
                        (y < (SceneLabel_y0[0] + SceneLabel_h[0]))) {
                        SceneLabelIlluminance[0] += static_cast<uint8_t>((0.299f * r) + (0.587f * g) + (0.114f * b));
                        SceneLabelCount[0]++;
                    }

                    /* Min Label */
                    if ((x_rot >= SceneLabel_x0[1]) &&
                        (x_rot < (SceneLabel_x0[1] + SceneLabel_w[1])) &&
                        (y >= SceneLabel_y0[1]) &&
                        (y < (SceneLabel_y0[1] + SceneLabel_h[1]))) {
                        SceneLabelIlluminance[1] += static_cast<uint8_t>((0.299f * r) + (0.587f * g) + (0.114f * b));
                        SceneLabelCount[1]++;
                    }

                    /* Mean Label */
                    if ((x_rot >= SceneLabel_x0[2]) &&
                        (x_rot < (SceneLabel_x0[2] + SceneLabel_w[2])) &&
                        (y >= SceneLabel_y0[2]) &&
                        (y < (SceneLabel_y0[2] + SceneLabel_h[2]))) {
                        SceneLabelIlluminance[2] += static_cast<uint8_t>((0.299f * r) + (0.587f * g) + (0.114f * b));
                        SceneLabelCount[2]++;
                    }

                    uint32_t dst_idx = (y * Image_Width) + x;

                    /* Convert to RGB565 - LVGL handles swapping with RGB565_SWAPPED */
                    uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

                    /* Low byte first */
                    Dst[(dst_idx * 2) + 0] = rgb565 & 0xFF;

                    /* High byte second */
                    Dst[(dst_idx * 2) + 1] = (rgb565 >> 8) & 0xFF;
                }
            }

            /* Set the average Luminance and the text color */
            for (uint8_t i = 0; i < (sizeof(SceneLabelAverageLuminance) / sizeof(SceneLabelAverageLuminance[0])); i++) {
                if (SceneLabelCount[i] > 0) {
                    SceneLabelAverageLuminance[i] = SceneLabelIlluminance[i] / SceneLabelCount[i];
                } else {
                    SceneLabelAverageLuminance[i] = 0;
                }
            }

            lv_obj_set_style_text_color(ui_Label_Main_Thermal_Scene_Max,
                                        (SceneLabelAverageLuminance[0] > 128) ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ui_Label_Main_Thermal_Scene_Min,
                                        (SceneLabelAverageLuminance[1] > 128) ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_color(ui_Label_Main_Thermal_Scene_Mean,
                                        (SceneLabelAverageLuminance[2] > 128) ? lv_color_black() : lv_color_white(), LV_PART_MAIN);

            /* Reset watchdog after image processing */
            esp_task_wdt_reset();

            /* Max temperature (top of gradient) */
            float temp_max_celsius = (LeptonFrame.Max / 100.0f) - 273.15f;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", temp_max_celsius);
            lv_label_set_text(ui_Label_TempScaleMax, Buffer);

            /* Min temperature (bottom of gradient) */
            float temp_min_celsius = (LeptonFrame.Min / 100.0f) - 273.15f;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", temp_min_celsius);
            lv_label_set_text(ui_Label_TempScaleMin, Buffer);

            /* Trigger LVGL to redraw the image */
            lv_obj_invalidate(ui_Image_Thermal);
            ESP_LOGD(TAG, "Updated thermal image display (src: %ux%u -> dst: %ux%u)", LeptonFrame.Width, LeptonFrame.Height,
                     Image_Width, Image_Height);

            /* Update network frame for server streaming if server is running */
            if (Server_isRunning()) {
                if (xSemaphoreTake(_GUITask_State.NetworkFrame.Mutex, 0) == pdTRUE) {
                    /* Convert scaled RGB565 buffer to RGB888 for network transmission */
                    uint8_t *rgb888_dst = _GUITask_State.NetworkRGBBuffer;
                    for (uint32_t i = 0; i < (Image_Width * Image_Height); i++) {
                        /* Read RGB565 value (little endian) */
                        uint16_t rgb565 = Dst[(i * 2) + 0] | (Dst[(i * 2) + 1] << 8);

                        /* Convert RGB565 to RGB888 */
                        uint8_t r = (rgb565 >> 8) & 0xF8;
                        uint8_t g = (rgb565 >> 3) & 0xFC;
                        uint8_t b = (rgb565 << 3) & 0xF8;

                        /* Store as RGB888 */
                        rgb888_dst[(i * 3) + 0] = r;
                        rgb888_dst[(i * 3) + 1] = g;
                        rgb888_dst[(i * 3) + 2] = b;
                    }

                    _GUITask_State.NetworkFrame.Buffer = _GUITask_State.NetworkRGBBuffer;
                    _GUITask_State.NetworkFrame.Width = Image_Width;
                    _GUITask_State.NetworkFrame.Height = Image_Height;
                    _GUITask_State.NetworkFrame.Timestamp = esp_timer_get_time() / 1000;

                    xSemaphoreGive(_GUITask_State.NetworkFrame.Mutex);
                }

                Server_NotifyClients();
            }
        }

        /* Process the recieved system events */
        EventBits = xEventGroupGetBits(_GUITask_State.EventGroup);
        if (EventBits & GUI_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _GUITask_State.isRunning = false;

            xEventGroupClearBits(_GUITask_State.EventGroup, GUI_TASK_STOP_REQUEST);

            break;
        } else if (EventBits & BATTERY_VOLTAGE_READY) {
            char Buffer[16];

            snprintf(Buffer, sizeof(Buffer), "%d%%", _GUITask_State.BatteryInfo.Percentage);

            if (_GUITask_State.BatteryInfo.Percentage == 100) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_FULL " %d%%", _GUITask_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0x00FF00), 0);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 75) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_3 " %d%%", _GUITask_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0x00FF00), 0);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 50) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_2 " %d%%", _GUITask_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFFFF00), 0);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 25) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_1 " %d%%", _GUITask_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFFFF00), 0);
            } else {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_EMPTY " %d%%", _GUITask_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFF0000), 0);
            }

            lv_label_set_text(ui_Label_Main_Battery_Remaining, Buffer);

            xEventGroupClearBits(_GUITask_State.EventGroup, BATTERY_VOLTAGE_READY);
        } else if ( EventBits & BATTERY_CHARGING_STATUS_READY) {
            xEventGroupClearBits(_GUITask_State.EventGroup, BATTERY_CHARGING_STATUS_READY);
        } else if (EventBits & WIFI_CONNECTION_STATE_CHANGED) {
            if (_GUITask_State.WiFiConnected) {
                char Buffer[16];

                snprintf(Buffer, sizeof(Buffer), "%lu.%lu.%lu.%lu",
                         (_GUITask_State.IP_Info.IP >> 0) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 8) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 16) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 24) & 0xFF);

                lv_label_set_text(ui_Label_Info_IP, Buffer);
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0x00FF00), LV_PART_MAIN);

                // TODO: Disable WiFi buttons in the menu
                //lv_obj_remove_flag(ui_Button_Main_WiFi, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_label_set_text(ui_Label_Info_IP, "Not connected");
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);

                // TODO: Disable WiFi buttons in the menu
                //lv_obj_add_flag(ui_Button_Main_WiFi, LV_OBJ_FLAG_CLICKABLE);
            }

            xEventGroupClearBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);
        } else if (EventBits & PROVISIONING_STATE_CHANGED) {
            if ((_GUITask_State.WiFiConnected == false) && _GUITask_State.ProvisioningActive) {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF8800), LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }

            xEventGroupClearBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);
        } else if (EventBits & SD_CARD_STATE_CHANGED) {
            ESP_LOGI(TAG, "SD card state changed: %s", _GUITask_State.CardPresent ? "present" : "removed");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_STATE_CHANGED);
        } else if (EventBits & SD_CARD_MOUNTED) {
            ESP_LOGI(TAG, "SD card mounted - updating GUI");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_MOUNTED);
        } else if (EventBits & SD_CARD_MOUNT_ERROR) {
            ESP_LOGE(TAG, "SD card mount failed - keeping card present status");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_MOUNT_ERROR);
        } else if (EventBits & LEPTON_SPOTMETER_READY) {
            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_SPOTMETER_READY);
        } else if (EventBits & LEPTON_UPTIME_READY) {
            char Buffer[32];
            uint32_t Uptime;

            Uptime = _GUITask_State.LeptonUptime / 1000;

            snprintf(Buffer, sizeof(Buffer), "%02lu:%02lu:%02lu", Uptime / 3600, (Uptime % 3600) / 60, Uptime % 60);
            lv_label_set_text(ui_Label_Info_Lepton_Uptime, Buffer);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_UPTIME_READY);
        } else if (EventBits & LEPTON_TEMP_READY) {
            char Buffer[32];

            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUITask_State.LeptonTemperatures.FPA);
            lv_label_set_text(ui_Label_Info_Lepton_FPA, Buffer);
            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUITask_State.LeptonTemperatures.AUX);
            lv_label_set_text(ui_Label_Info_Lepton_AUX, Buffer);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_TEMP_READY);
        } else if (EventBits & LEPTON_PIXEL_TEMPERATURE_READY) {
            char Buffer[16];

            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUITask_State.SpotTemperature);
            lv_label_set_text(ui_Label_Main_Thermal_PixelTemperature, Buffer);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_PIXEL_TEMPERATURE_READY);
        } else if (EventBits & LEPTON_SCENE_STATISTICS_READY) {
            char Buffer[16];

            float temp_max_celsius = _GUITask_State.ROIResult.Max;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", temp_max_celsius);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Max, Buffer);

            float temp_min_celsius = _GUITask_State.ROIResult.Min;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", temp_min_celsius);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Min, Buffer);

            float temp_mean_celsius = _GUITask_State.ROIResult.Mean ;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", temp_mean_celsius);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Mean, Buffer);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_SCENE_STATISTICS_READY);
        }

        _lock_acquire(&_GUITask_State.LVGL_API_Lock);
        uint32_t time_till_next = lv_timer_handler();
        _lock_release(&_GUITask_State.LVGL_API_Lock);
        uint32_t delay_ms = (time_till_next > 0) ? time_till_next : 10;

        esp_task_wdt_reset();

        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    }

    _GUITask_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t GUI_Task_Init(void)
{
    if (_GUITask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_ERROR_CHECK(GUI_Helper_Init(&_GUITask_State, Touch_LVGL_ReadCallback));

    ui_init();

    _GUITask_State.ThermalCanvasBuffer = (uint8_t *)heap_caps_malloc(240 * 180 * 2, MALLOC_CAP_SPIRAM);
    _GUITask_State.GradientCanvasBuffer = (uint8_t *)heap_caps_malloc(20 * 180 * 2, MALLOC_CAP_SPIRAM);
    _GUITask_State.NetworkRGBBuffer = (uint8_t *)heap_caps_malloc(240 * 180 * 3, MALLOC_CAP_SPIRAM);

    if (_GUITask_State.ThermalCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate thermal canvas buffer!");

        return ESP_ERR_NO_MEM;
    }

    if (_GUITask_State.GradientCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate gradient canvas buffer!");
        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITask_State.NetworkRGBBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network RGB buffer!");
        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        heap_caps_free(_GUITask_State.GradientCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    /* Create image save queue and task */
    _ImageSaveQueue = xQueueCreate(1, sizeof(App_Lepton_FrameReady_t));
    if (_ImageSaveQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create image save queue!");
        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        heap_caps_free(_GUITask_State.GradientCanvasBuffer);
        heap_caps_free(_GUITask_State.NetworkRGBBuffer);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t Result = xTaskCreatePinnedToCore(
                            Task_ImageSave,
                            "Task_ImgSave",
                            8192,
                            NULL,
                            CONFIG_GUI_TASK_PRIO - 1,  /* Lower priority than GUI */
                            &_ImageSaveTaskHandle,
                            1
                        );

    if (Result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create image save task!");

        vQueueDelete(_ImageSaveQueue);
        _ImageSaveQueue = NULL;

        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        heap_caps_free(_GUITask_State.GradientCanvasBuffer);
        heap_caps_free(_GUITask_State.NetworkRGBBuffer);

        return ESP_FAIL;
    }

    /* Initialize buffers with black pixels (RGB565 = 0x0000) */
    memset(_GUITask_State.ThermalCanvasBuffer, 0x00, 240 * 180 * 2);
    memset(_GUITask_State.GradientCanvasBuffer, 0x00, 20 * 180 * 2);

    /* Now configure the image descriptors with allocated buffers */
    _GUITask_State.ThermalImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITask_State.ThermalImageDescriptor.header.w = 240;
    _GUITask_State.ThermalImageDescriptor.header.h = 180;
    _GUITask_State.ThermalImageDescriptor.data = _GUITask_State.ThermalCanvasBuffer;
    _GUITask_State.ThermalImageDescriptor.data_size = 240 * 180 * 2;

    _GUITask_State.GradientImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITask_State.GradientImageDescriptor.header.w = 20;
    _GUITask_State.GradientImageDescriptor.header.h = 180;
    _GUITask_State.GradientImageDescriptor.data = _GUITask_State.GradientCanvasBuffer;
    _GUITask_State.GradientImageDescriptor.data_size = 20 * 180 * 2;

    UI_Canvas_AddTempGradient();

    /* Set the images */
    lv_img_set_src(ui_Image_Thermal, &_GUITask_State.ThermalImageDescriptor);
    lv_img_set_src(ui_Image_Gradient, &_GUITask_State.GradientImageDescriptor);

    /* Initialize network frame for server streaming */
    _GUITask_State.NetworkFrame.Mutex = xSemaphoreCreateMutex();
    if (_GUITask_State.NetworkFrame.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create NetworkFrame mutex!");

        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        heap_caps_free(_GUITask_State.GradientCanvasBuffer);
        heap_caps_free(_GUITask_State.NetworkRGBBuffer);

        return ESP_ERR_NO_MEM;
    }

    /* Use the event loop to receive control signals from other tasks */
    esp_event_handler_register(DEVICE_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler, NULL);
    esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
    esp_event_handler_register(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler, NULL);
    esp_event_handler_register(TIME_EVENTS, ESP_EVENT_ANY_ID, on_Time_Event_Handler, NULL);
    esp_event_handler_register(MEMORY_EVENTS, ESP_EVENT_ANY_ID, on_SD_Event_Handler, NULL);

    _GUITask_State.isInitialized = true;

    return ESP_OK;
}

void GUI_Task_Deinit(void)
{
    if (_GUITask_State.isInitialized == false) {
        return;
    }

    esp_event_handler_unregister(DEVICE_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler);
    esp_event_handler_unregister(TIME_EVENTS, ESP_EVENT_ANY_ID, on_Time_Event_Handler);
    esp_event_handler_unregister(MEMORY_EVENTS, ESP_EVENT_ANY_ID, on_SD_Event_Handler);

    ui_destroy();

    GUI_Helper_Deinit(&_GUITask_State);

    if (_GUITask_State.NetworkFrame.Mutex != NULL) {
        vSemaphoreDelete(_GUITask_State.NetworkFrame.Mutex);
        _GUITask_State.NetworkFrame.Mutex = NULL;
    }

    _GUITask_State.Display = NULL;
    _GUITask_State.isInitialized = false;
}

esp_err_t GUI_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t ret;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_GUITask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_GUITask_State.isRunning) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    _GUITask_State.isRunning = true;

    ESP_LOGD(TAG, "Starting GUI Task");

    ret = xTaskCreatePinnedToCore(
              Task_GUI,
              "Task_GUI",
              CONFIG_GUI_TASK_STACKSIZE,
              p_AppContext,
              CONFIG_GUI_TASK_PRIO,
              &_GUITask_State.TaskHandle,
              CONFIG_GUI_TASK_CORE
          );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task: %d!", ret);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t GUI_Task_Stop(void)
{
    if (_GUITask_State.isRunning == false) {
        return ESP_OK;
    }

    xEventGroupSetBits(_GUITask_State.EventGroup, GUI_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool GUI_Task_isRunning(void)
{
    return _GUITask_State.isRunning;
}

/** @brief Image save task - runs in background to avoid blocking GUI.
 */
static void Task_ImageSave(void *p_Param)
{
    App_Lepton_FrameReady_t Frame;
    char FilePath[128];
    FILE *File = NULL;

    ESP_LOGI(TAG, "Image save task started");

    while (true) {
        /* Wait for save request */
        if (xQueueReceive(_ImageSaveQueue, &Frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "=== Background Image Save START ===");

        /* Check if filesystem is locked (USB active) */
        if (MemoryManager_IsFilesystemLocked()) {
            ESP_LOGW(TAG, "Cannot save image - USB mode active!");
            esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
            continue;
        }

        /* Validate frame data */
        if ((Frame.Buffer == NULL) || (Frame.Width == 0) || (Frame.Height == 0)) {
            ESP_LOGE(TAG, "Invalid frame data!");
            esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
            continue;
        }

        const char *p_StoragePath = MemoryManager_GetStoragePath();
        static uint32_t ImageCounter = 0;
        snprintf(FilePath, sizeof(FilePath), "%s/IMG_%03u.BMP", p_StoragePath, (unsigned int)(ImageCounter % 1000));
        ImageCounter++;

        ESP_LOGI(TAG, "Saving: %s (%dx%d)", FilePath, Frame.Width, Frame.Height);

        /* Open file */
        File = fopen(FilePath, "wb");
        if (File == NULL) {
            ESP_LOGE(TAG, "Failed to open file: errno=%d (%s)", errno, strerror(errno));
            esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
            continue;
        }

        /* BMP file header */
        uint32_t ImageSize = Frame.Width * Frame.Height * 3;
        uint32_t FileSize = 54 + ImageSize;
        uint8_t BMPHeader[54] = {
            'B', 'M',
            (uint8_t)(FileSize), (uint8_t)(FileSize >> 8), (uint8_t)(FileSize >> 16), (uint8_t)(FileSize >> 24),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            (uint8_t)(Frame.Width), (uint8_t)(Frame.Width >> 8), (uint8_t)(Frame.Width >> 16), (uint8_t)(Frame.Width >> 24),
            (uint8_t)(Frame.Height), (uint8_t)(Frame.Height >> 8), (uint8_t)(Frame.Height >> 16), (uint8_t)(Frame.Height >> 24),
            1, 0, 24, 0, 0, 0, 0, 0,
            (uint8_t)(ImageSize), (uint8_t)(ImageSize >> 8), (uint8_t)(ImageSize >> 16), (uint8_t)(ImageSize >> 24),
            0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        };

        if (fwrite(BMPHeader, 1, 54, File) != 54) {
            ESP_LOGE(TAG, "Failed to write header");
            fclose(File);
            esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
            continue;
        }

        /* Write pixel data */
        uint8_t *LineBuffer = (uint8_t *)heap_caps_malloc(Frame.Width * 3, MALLOC_CAP_8BIT);
        if (LineBuffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate line buffer");
            fclose(File);
            esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
            continue;
        }

        for (int32_t y = 0; y < Frame.Height; y++) {
            uint8_t *SrcLine = Frame.Buffer + (y * Frame.Width * Frame.Channels);
            for (uint32_t x = 0; x < Frame.Width; x++) {
                uint32_t dstX = (Frame.Width - 1) - x;
                LineBuffer[dstX * 3 + 0] = SrcLine[x * Frame.Channels + 2];
                LineBuffer[dstX * 3 + 1] = SrcLine[x * Frame.Channels + 1];
                LineBuffer[dstX * 3 + 2] = SrcLine[x * Frame.Channels + 0];
            }
            if (fwrite(LineBuffer, 1, Frame.Width * 3, File) != Frame.Width * 3) {
                ESP_LOGE(TAG, "Write failed at line %d", y);
                heap_caps_free(LineBuffer);
                fclose(File);
                esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVE_FAILED, NULL, 0, portMAX_DELAY);
                goto next_save;
            }
        }

        heap_caps_free(LineBuffer);
        fclose(File);

        ESP_LOGI(TAG, "✓ Image saved: %s (%d bytes)", FilePath, FileSize);
        esp_event_post(GUI_EVENTS, GUI_EVENT_IMAGE_SAVED, NULL, 0, portMAX_DELAY);

next_save:
        continue;
    }
}

esp_err_t GUI_SaveThermalImage(void)
{
    char FilePath[128];
    char TimeStr[32];
    FILE *File = NULL;
    time_t Now;
    struct tm TimeInfo;
    App_Lepton_FrameReady_t Frame;

    /* Check if AppContext is valid */
    if (_GUITask_State.p_AppContext == NULL) {
        ESP_LOGE(TAG, "AppContext is NULL!");
        return ESP_ERR_INVALID_STATE;
    } else if (_GUITask_State.p_AppContext->Lepton_FrameEventQueue == NULL) {
        ESP_LOGE(TAG, "Frame queue is NULL!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Abort if filesystem is locked (USB active) */
    if (MemoryManager_IsFilesystemLocked()) {
        ESP_LOGW(TAG, "Cannot save image - USB mode active!");

        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Filesystem OK, checking frame queue...");

    /* Try to get latest frame from queue (non-blocking) */
    if (xQueuePeek(_GUITask_State.p_AppContext->Lepton_FrameEventQueue, &Frame, 0) != pdTRUE) {
        ESP_LOGW(TAG, "No thermal frame available in queue!");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Frame available - Width: %d, Height: %d, Channels: %d, Buffer: %p",
             Frame.Width, Frame.Height, Frame.Channels, Frame.Buffer);

    /* Validate frame data */
    if ((Frame.Buffer == NULL) || (Frame.Width == 0) || (Frame.Height == 0)) {
        ESP_LOGE(TAG, "Invalid frame data! Buffer=%p, W=%d, H=%d",
                 Frame.Buffer, Frame.Width, Frame.Height);
        return ESP_ERR_INVALID_ARG;
    }

    /* Get current time for filename */
    time(&Now);
    localtime_r(&Now, &TimeInfo);
    strftime(TimeStr, sizeof(TimeStr), "%Y%m%d_%H%M%S", &TimeInfo);

    /* Create filename with timestamp */
    const char *p_StoragePath = MemoryManager_GetStoragePath();
    ESP_LOGI(TAG, "Storage path: %s", p_StoragePath);

    snprintf(FilePath, sizeof(FilePath), "%s/THERMAL_%s.BMP", p_StoragePath, TimeStr);

    ESP_LOGI(TAG, "Attempting to save: %s", FilePath);
    ESP_LOGI(TAG, "  Resolution: %dx%d, Channels: %d", Frame.Width, Frame.Height, Frame.Channels);

    /* Use simple filename format: IMG_XXX.BMP with counter instead of timestamp */
    ESP_LOGI(TAG, "Using storage path: %s", p_StoragePath);

    static uint32_t ImageCounter = 0;
    snprintf(FilePath, sizeof(FilePath), "%s/IMG_%03u.BMP", p_StoragePath, (unsigned int)(ImageCounter % 1000));
    ImageCounter++;

    /* Open file for writing */
    ESP_LOGI(TAG, "Opening file: %s", FilePath);
    File = fopen(FilePath, "wb");
    if (File == NULL) {
        int err = errno;
        ESP_LOGE(TAG, "Failed to open file: %s", FilePath);
        ESP_LOGE(TAG, "  errno=%d (%s)", err, strerror(err));
        ESP_LOGE(TAG, "  Check: Is filesystem mounted? Is path correct?");
        ESP_LOGE(TAG, "  Storage path: %s", p_StoragePath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File opened successfully, writing BMP header...");

    /* BMP file header (14 bytes) */
    uint32_t ImageSize = Frame.Width * Frame.Height * 3;  /* RGB24 */
    uint32_t FileSize = 54 + ImageSize;  /* Header + pixel data */
    uint8_t BMPHeader[54] = {
        /* BMP Header (14 bytes) */
        'B', 'M',                           /* Signature */
        (uint8_t)(FileSize),                /* File size */
        (uint8_t)(FileSize >> 8),
        (uint8_t)(FileSize >> 16),
        (uint8_t)(FileSize >> 24),
        0, 0, 0, 0,                         /* Reserved */
        54, 0, 0, 0,                        /* Offset to pixel data */

        /* DIB Header (40 bytes - BITMAPINFOHEADER) */
        40, 0, 0, 0,                        /* DIB header size */
        (uint8_t)(Frame.Width),             /* Width */
        (uint8_t)(Frame.Width >> 8),
        (uint8_t)(Frame.Width >> 16),
        (uint8_t)(Frame.Width >> 24),
        (uint8_t)(Frame.Height),            /* Height */
        (uint8_t)(Frame.Height >> 8),
        (uint8_t)(Frame.Height >> 16),
        (uint8_t)(Frame.Height >> 24),
        1, 0,                               /* Color planes */
        24, 0,                              /* Bits per pixel (RGB24) */
        0, 0, 0, 0,                         /* Compression (none) */
        (uint8_t)(ImageSize),               /* Image size */
        (uint8_t)(ImageSize >> 8),
        (uint8_t)(ImageSize >> 16),
        (uint8_t)(ImageSize >> 24),
        0x13, 0x0B, 0, 0,                   /* Horizontal resolution (2835 pixels/m) */
        0x13, 0x0B, 0, 0,                   /* Vertical resolution (2835 pixels/m) */
        0, 0, 0, 0,                         /* Colors in palette */
        0, 0, 0, 0,                         /* Important colors */
    };

    /* Write BMP header */
    size_t HeaderWritten = fwrite(BMPHeader, 1, 54, File);
    if (HeaderWritten != 54) {
        ESP_LOGE(TAG, "Failed to write BMP header! Written: %zu/54", HeaderWritten);

        fclose(File);

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Header written, writing pixel data...");

    /* BMP stores pixels bottom-to-top, BGR format */
    /* Convert RGB to BGR and write line by line from bottom to top */
    uint8_t *LineBuffer = static_cast<uint8_t *>(heap_caps_malloc(Frame.Width * 3, MALLOC_CAP_8BIT));
    if (LineBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate line buffer!");

        fclose(File);

        return ESP_ERR_NO_MEM;
    }

    /* BMP is bottom-to-top, but we need to flip to correct orientation */
    for (int32_t y = 0; y < Frame.Height; y++) {
        uint8_t *SrcLine = Frame.Buffer + (y * Frame.Width * Frame.Channels);

        /* Convert RGB to BGR and flip horizontally */
        for (uint32_t x = 0; x < Frame.Width; x++) {
            uint32_t dstX = (Frame.Width - 1) - x;  /* Flip horizontal */
            LineBuffer[dstX * 3 + 0] = SrcLine[x * Frame.Channels + 2];  /* B */
            LineBuffer[dstX * 3 + 1] = SrcLine[x * Frame.Channels + 1];  /* G */
            LineBuffer[dstX * 3 + 2] = SrcLine[x * Frame.Channels + 0];  /* R */
        }

        size_t LineWritten = fwrite(LineBuffer, 1, Frame.Width * 3, File);
        if (LineWritten != Frame.Width * 3) {
            ESP_LOGE(TAG, "Failed to write pixel data at line %d! Written: %zu/%d",
                     y, LineWritten, Frame.Width * 3);
            heap_caps_free(LineBuffer);
            fclose(File);
            return ESP_FAIL;
        }

        /* Reset watchdog every 10 lines to prevent timeout during slow writes */
        if ((y % 10) == 0) {
            esp_task_wdt_reset();
        }
    }

    heap_caps_free(LineBuffer);
    fclose(File);

    ESP_LOGI(TAG, "Thermal image saved successfully!");
    ESP_LOGI(TAG, "  File: %s", FilePath);
    ESP_LOGI(TAG, "  Size: %d bytes", FileSize);

    return ESP_OK;
}