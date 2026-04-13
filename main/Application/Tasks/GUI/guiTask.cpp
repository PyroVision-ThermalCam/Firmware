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
#include "Private/guiImageSave.h"
#include "UI/ui_messagebox.h"
#include "UI/ui_settings.h"
#include "Application/Tasks/Camera/cameraTask.h"

#include "lepton.h"

ESP_EVENT_DEFINE_BASE(GUI_TASK_EVENTS);

GUI_Task_State_t _GUI_Task_State;

static const char *TAG = "GUI-Task";

/** @brief                  Event handler for the Lepton task events to receive updates when Lepton events are triggered (e.g., new frame ready, camera errors).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Lepton_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Lepton task event received: ID=%d", ID);

    switch (ID) {
        case LEPTON_TASK_EVENT_CAMERA_READY: {
            memcpy(&_GUI_Task_State.LeptonDeviceInfo, static_cast<const App_Lepton_Device_t *>(p_Data),
                   sizeof(App_Lepton_Device_t));

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_READY);

            break;
        }
        case LEPTON_TASK_EVENT_CAMERA_ERROR: {
            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_ERROR);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_FPA_AUX_TEMP: {
            memcpy(&_GUI_Task_State.LeptonTemperatures, p_Data, sizeof(App_Lepton_Temperatures_t));

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_TEMP_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_SCENE_STATISTICS: {
            memcpy(&_GUI_Task_State.ROIResult, p_Data, sizeof(App_Lepton_ROI_Result_t));

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_SCENE_STATISTICS_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_UPTIME: {
            memcpy(&_GUI_Task_State.LeptonUptime, p_Data, sizeof(uint32_t));

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_UPTIME_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_PIXEL_TEMPERATURE: {
            memcpy(&_GUI_Task_State.SpotTemperature, p_Data, sizeof(float));

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled Lepton event ID: 0x%X", ID);

            break;
        }
    }
}

/** @brief                  Event handler for camera task events to receive async init result.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Camera_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case CAMERA_EVENT_INIT_COMPLETE: {
            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_CAMERA_READY);

            break;
        }
        case CAMERA_EVENT_INIT_FAILED: {
            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_CAMERA_ERROR);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Event handler for GUI task events (e.g., image save completion).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI task event received: ID=%d", ID);

    switch (ID) {
        case GUI_TASK_EVENT_THERMAL_IMAGE_SAVED: {
            ESP_LOGD(TAG, "Thermal image saved successfully");

            break;
        }
        case GUI_TASK_EVENT_THERMAL_IMAGE_SAVE_FAILED: {
            ESP_LOGE(TAG, "Thermal image save failed");

            break;
        }
    }
}

/** @brief                  Event handler for the Devices task events to receive updates when settings are changed.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Devices_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Devices task event received: ID=%d", ID);

    switch (ID) {
        case DEVICES_TASK_EVENT_RESPONSE_BATTERY: {
            memcpy(&_GUI_Task_State.BatteryInfo, p_Data, sizeof(App_Devices_Battery_t));

            ESP_LOGD(TAG, "Battery status updated: Voltage=%dmV, Percentage=%d%%, Charging=%s",
                     _GUI_Task_State.BatteryInfo.Voltage, _GUI_Task_State.BatteryInfo.Percentage,
                     _GUI_Task_State.BatteryInfo.Charging ? "Yes" : "No");

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_BATTERY_STATUS_CHANGED);

            break;
        }
    }
}

/** @brief                  Event handler for the Devices events to receive updates when settings are changed.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Devices_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Devices event received: ID=%d", ID);

    switch (ID) {
        case DEVICES_EVENT_SD_DETECT: {
            _GUI_Task_State.CardPresent = *static_cast<const bool *>(p_Data);

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_SD_CARD_STATE_CHANGED);

            break;
        }
    }
}

/** @brief                  Event handler for the Network events to receive updates when network events are triggered (e.g., WiFi connection changes).
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
            memcpy(&_GUI_Task_State.IP_Info, p_Data, sizeof(Network_IP_Info_t));
            _GUI_Task_State.WiFiConnected = true;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            _GUI_Task_State.WiFiConnected = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STARTED: {
            _GUI_Task_State.ProvisioningActive = true;
            _GUI_Task_State.WiFiConnected = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STOPPED: {
            _GUI_Task_State.ProvisioningActive = false;
            _GUI_Task_State.WiFiConnected = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            _GUI_Task_State.ProvisioningActive = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            _GUI_Task_State.ProvisioningActive = false;
            _GUI_Task_State.WiFiConnected = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_SERVER_STARTED: {
            ESP_LOGD(TAG, "Network frame registered with server");

            /* Register thermal frame with server (called after server is started) */
            Server_SetThermalFrame(&_GUI_Task_State.NetworkFrame);

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

/** @brief                  Event handler for the USB events to receive UVC streaming state changes.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_USB_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "USB event received in GUI: ID=%d", ID);

    switch (ID) {
        case USB_EVENT_UVC_STREAMING_START: {
            _GUI_Task_State.isUVCStreaming = true;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);

            break;
        }
        case USB_EVENT_UVC_STREAMING_STOP: {
            _GUI_Task_State.isUVCStreaming = false;

            xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            if (_GUI_Task_State.isUVCStreaming) {
                _GUI_Task_State.isUVCStreaming = false;

                xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);
            }

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief Update the information screen labels.
 */
static void GUI_Update_Info(void)
{
    uint8_t MAC[6];
    char Buffer[32];

    esp_efuse_mac_get_default(MAC);
    snprintf(Buffer, sizeof(Buffer), "%02X:%02X:%02X:%02X:%02X:%02X", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);

    lv_label_set_text(ui_Label_Info_Lepton_Serial, _GUI_Task_State.LeptonDeviceInfo.SerialNumber);
    lv_label_set_text(ui_Label_Info_Lepton_Part, _GUI_Task_State.LeptonDeviceInfo.PartNumber);
    lv_label_set_text(ui_Label_Info_Lepton_GPP_Revision, _GUI_Task_State.LeptonDeviceInfo.SoftwareRevision.GPP_Revision);
    lv_label_set_text(ui_Label_Info_Lepton_DSP_Revision, _GUI_Task_State.LeptonDeviceInfo.SoftwareRevision.DSP_Revision);
    lv_label_set_text(ui_Label_Info_MAC, Buffer);
}

/** @brief      Update the ROI rectangle on the GUI and on the Lepton.
 *  @param Type ROI type
 *  @param x    X position of the ROI (in Lepton coordinates 0-159)
 *  @param y    Y position (in Lepton coordinates 0-119)
 *  @param w    Width of the ROI
 *  @param h    Height of the ROI
 */
static void GUI_Update_ROI(Settings_ROI_t ROI)
{
    int32_t DisplayWidth;
    int32_t DisplayHeight;
    Settings_Lepton_t SettingsLepton;

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
            ESP_LOGW(TAG, "Invalid GUI ROI type: 0x%X", ROI.Type);

            return;
        }
    }

    SettingsManager_GetLepton(&SettingsLepton);

    /* Check if an update is required */
    if ((SettingsLepton.ROI[ROI.Type].x == ROI.x) &&
        (SettingsLepton.ROI[ROI.Type].y == ROI.y) &&
        (SettingsLepton.ROI[ROI.Type].w == ROI.w) &&
        (SettingsLepton.ROI[ROI.Type].h == ROI.h)) {
        ESP_LOGD(TAG, "ROI unchanged, not updating NVS");

        return;
    }

    /* Copy the new ROI in the existing settings structure */
    memcpy(&SettingsLepton.ROI[ROI.Type], &ROI, sizeof(Settings_ROI_t));

    /* Save the new ROI to NVS */
    // TODO: Fix me. Causes crashes during boot
    //SettingsManager_UpdateLepton(&SettingsLepton);
    SettingsManager_Save();

    /* The Lepton task needs the ROI with the Lepton coordinates */
    SettingsLepton.ROI[ROI.Type] = {
        .Type = ROI.Type,
        .x = static_cast<uint16_t>(ROI.x),
        .y = static_cast<uint16_t>(ROI.y),
        .w = static_cast<uint16_t>(ROI.w),
        .h = static_cast<uint16_t>(ROI.h)
    };

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_ROI, &SettingsLepton.ROI[ROI.Type], sizeof(Settings_ROI_t),
                   pdMS_TO_TICKS(100));
}

/** @brief Create temperature gradient canvas for palette visualization.
 *         Generates a vertical gradient from hot (top) to cold (bottom).
 */
static void UI_Canvas_AddTempGradient(void)
{
    /* Generate gradient pixel by pixel */
    uint16_t *Buffer = reinterpret_cast<uint16_t *>(_GUI_Task_State.GradientCanvasBuffer);

    for (uint32_t y = 0; y < _GUI_Task_State.GradientImageDescriptor.header.h; y++) {
        uint32_t Index;

        /* Map y position to palette index (0 = top/hot = white, 179 = bottom/cold = black)
         * Iron palette: index 0 = black (cold), index 255 = white (hot)
         * So we need to invert: top (y=0) should be index 255, bottom (y=179) should be index 0
         */
        Index = 255 - (y * 255 / (_GUI_Task_State.GradientImageDescriptor.header.h - 1));

        /* Get RGB888 values from palette */
        uint8_t r8 = Lepton_Palette_Iron[Index][0];
        uint8_t g8 = Lepton_Palette_Iron[Index][1];
        uint8_t b8 = Lepton_Palette_Iron[Index][2];

        /* Convert RGB888 to RGB565 */
        uint16_t r5 = (r8 >> 3) & 0x1F;
        uint16_t g6 = (g8 >> 2) & 0x3F;
        uint16_t b5 = (b8 >> 3) & 0x1F;

        /* Fill entire row with same color */
        for (uint32_t x = 0; x < _GUI_Task_State.GradientImageDescriptor.header.w; x++) {
            Buffer[y * _GUI_Task_State.GradientImageDescriptor.header.w + x] = (r5 << 11) | (g6 << 5) | b5;
        }
    }
}

/** @brief          LVGL keypad read callback.
 *                  Maps the debounced displayboard input state (joystick + buttons) to a
 *                  single LVGL key event. Button priority: joystick directions first, then
 *                  joystick center, then Button1–4.
 *                  Key mapping:
 *                    JoyUp -> LV_KEY_UP
 *                    JoyDown -> LV_KEY_DOWN
 *                    JoyLeft -> LV_KEY_LEFT
 *                    JoyRight -> LV_KEY_RIGHT
 *                    JoyCenter / Button1 -> LV_KEY_ENTER
 *                    Button2 -> LV_KEY_ESC
 *                    Button3 -> LV_KEY_NEXT
 *                    Button4 -> LV_KEY_PREV
 *  @param p_Indev  Input device handle
 *  @param p_Data   Input device data
 */
static void Keypad_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    Devices_InputState_t State;
    uint32_t Key = 0;
    bool Pressed = false;

    if ((_GUI_Task_State.AppContext == NULL) || (_GUI_Task_State.AppContext->InputMutex == NULL)) {
        p_Data->state = LV_INDEV_STATE_RELEASED;

        return;
    }

    xSemaphoreTake(_GUI_Task_State.AppContext->InputMutex, portMAX_DELAY);
    memcpy(&State, &_GUI_Task_State.AppContext->InputState, sizeof(Devices_InputState_t));
    xSemaphoreGive(_GUI_Task_State.AppContext->InputMutex);

    /* First matching active input wins */
    if (State.JoyUp)         {
        Key = LV_KEY_UP;
        Pressed = true;
    } else if (State.JoyDown)  {
        Key = LV_KEY_DOWN;
        Pressed = true;
    } else if (State.JoyLeft)  {
        Key = LV_KEY_LEFT;
        Pressed = true;
    } else if (State.JoyRight) {
        Key = LV_KEY_RIGHT;
        Pressed = true;
    } else if (State.JoyCenter) {
        Key = LV_KEY_ENTER;
        Pressed = true;
    } else if (State.Button1)  {
        Key = LV_KEY_ENTER;
        Pressed = true;
    } else if (State.Button2)  {
        Key = LV_KEY_ESC;
        Pressed = true;
    } else if (State.Button3)  {
        Key = LV_KEY_NEXT;
        Pressed = true;
    } else if (State.Button4)  {
        Key = LV_KEY_PREV;
        Pressed = true;
    }

    p_Data->key = Key;
    p_Data->state = Pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    /* Log only on edge transitions to avoid spamming every LVGL tick */
    static bool PrevPressed = false;
    static uint32_t PrevKey = 0;

    if ((Pressed != PrevPressed) || (Pressed && (Key != PrevKey))) {
        if (Pressed) {
            const char *p_KeyName;

            switch (Key) {
                case LV_KEY_UP:
                    p_KeyName = "UP";
                    break;
                case LV_KEY_DOWN:
                    p_KeyName = "DOWN";
                    break;
                case LV_KEY_LEFT:
                    p_KeyName = "LEFT";
                    break;
                case LV_KEY_RIGHT:
                    p_KeyName = "RIGHT";
                    break;
                case LV_KEY_ENTER:
                    p_KeyName = "ENTER";
                    break;
                case LV_KEY_ESC:
                    p_KeyName = "ESC";
                    break;
                case LV_KEY_NEXT:
                    p_KeyName = "NEXT";
                    break;
                case LV_KEY_PREV:
                    p_KeyName = "PREV";
                    break;
                default:
                    p_KeyName = "?";
                    break;
            }

            ESP_LOGI(TAG, "Keypad: %s pressed", p_KeyName);
        } else {
            ESP_LOGI(TAG, "Keypad: released");
        }
    }

    PrevPressed = Pressed;
    PrevKey = Key;
}

/** @brief          LVGL touch read callback.
 *  @param p_Indev  Input device handle
 *  @param p_Data   Input device data
 */
static void Touch_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    uint8_t Count;
    esp_lcd_touch_point_data_t Data[1];
    esp_lcd_touch_handle_t Touch;

    Touch = static_cast<esp_lcd_touch_handle_t>(lv_indev_get_user_data(p_Indev));

    if ((esp_lcd_touch_read_data(Touch) == ESP_OK) &&
        (esp_lcd_touch_get_data(Touch, Data, &Count, sizeof(Data) / sizeof(Data[0])) == ESP_OK) &&
        (Count > 0)
       ) {
        p_Data->point.x = CONFIG_GUI_WIDTH - Data[0].x;
        p_Data->point.y = Data[0].y;
        p_Data->state = LV_INDEV_STATE_PRESSED;

        ESP_LOGD(TAG, "Raw: (%d, %d)", Data[0].x, Data[0].y);
        ESP_LOGD(TAG, "Mapped: (%d, %d)", p_Data->point.x, p_Data->point.y);
    } else {
        p_Data->state = LV_INDEV_STATE_RELEASED;
    }
}

/** @brief              GUI Task main function.
 *  @param p_Parameters Task parameters
 */
void Task_GUI(void *p_Parameters)
{
    uint32_t Timeout;
    App_Context_t *App_Context;
    Settings_Lepton_t LeptonSettings;

    /* Precompute x bilinear coefficients once per frame (constant across all rows).
     * Saves ImageHeight (180) redundant divisions per pixel column.
     * Stack cost: 3 * CONFIG_GUI_WIDTH = 960 bytes.
     * NOTE: x_lut_xi is NOT stored - computing 256 - x_frac inline avoids a uint8_t
     * overflow: when x_frac == 0, 256 would truncate to 0, zeroing all weights leads to black
     * pixels. x_inv is computed as uint32_t in the inner loop instead.
     */
    uint8_t x_lut_x0[CONFIG_GUI_WIDTH];
    uint8_t x_lut_x1[CONFIG_GUI_WIDTH];
    uint8_t x_lut_xf[CONFIG_GUI_WIDTH];

    esp_task_wdt_add(NULL);

    App_Context = static_cast<App_Context_t *>(p_Parameters);
    _GUI_Task_State.AppContext = App_Context;
    ESP_LOGD(TAG, "GUI Task started on core %d", xPortGetCoreID());

    /* Show splash screen first and wait for all components to become ready before starting the application.
     *
     * Sequencing:
     *   - Camera init runs as a background task (Camera_Task_InitAsync) and posts
     *     CAMERA_EVENT_INIT_COMPLETE / CAMERA_EVENT_INIT_FAILED when done (~0–2 s).
     *     On receipt, the bar snaps to 50 % ("Camera ready") if not yet past that value.
     *   - The Lepton requires ~5 s to boot. During the boot window the bar is animated
     *     at 2 %/100 ms so it naturally reaches ~99 % just as the LEPTON_READY event
     *     arrives and snaps the bar to 100 % -> app starts.
     *
     * Status text milestones (time-based):
     *    0 %  -> "Starting camera..."
     *   55 %  -> "Starting Lepton..."
     *   80 %  -> "Almost ready..."
     * Camera event -> "Camera ready" (only if bar < 50 %)
     */
    static const struct {
        int32_t Threshold;
        const char *Text;
    } SPLASH_MILESTONES[] = {
        {  0, "Starting camera..."  },
        { 55, "Starting Lepton..."  },
        { 80, "Almost ready..."     },
    };

    Timeout = 0;
    do {
        EventBits_t EventBits;
        int32_t CurrentBarValue;

        esp_task_wdt_reset();

        CurrentBarValue = lv_bar_get_value(ui_SplashScreen_LoadingBar);

        /* Advance bar by 2 % per 100 ms iteration (fills 0 -> 99 in ~5 s, matching Lepton boot time).
         * Do not auto-advance past 99 % — wait for the LEPTON_READY event to set it to 100 %. */
        if (CurrentBarValue < 99) {
            int32_t NextValue = CurrentBarValue + 2;

            if (NextValue > 99) {
                NextValue = 99;
            }

            lv_bar_set_value(ui_SplashScreen_LoadingBar, NextValue, LV_ANIM_OFF);

            /* Update status text when crossing a milestone threshold */
            for (int i = static_cast<int>(sizeof(SPLASH_MILESTONES) / sizeof(SPLASH_MILESTONES[0])) - 1; i >= 0; i--) {
                if (NextValue >= SPLASH_MILESTONES[i].Threshold && CurrentBarValue < SPLASH_MILESTONES[i].Threshold) {
                    lv_label_set_text(ui_SplashScreen_StatusText, SPLASH_MILESTONES[i].Text);

                    break;
                }
            }

            CurrentBarValue = NextValue;
        }

        EventBits = xEventGroupGetBits(_GUI_Task_State.EventGroup);
        if (EventBits & GUI_TASK_CAMERA_READY) {
            /* Camera async init completed. Snap bar to 50 % milestone if not already past it. */
            if (CurrentBarValue < 50) {
                lv_bar_set_value(ui_SplashScreen_LoadingBar, 50, LV_ANIM_OFF);
                lv_label_set_text(ui_SplashScreen_StatusText, "Camera ready");
            }

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_CAMERA_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_READY) {
            lv_bar_set_value(ui_SplashScreen_LoadingBar, 100, LV_ANIM_OFF);

            /* Lepton has finished booting. Initialize GT911 now - GT911 was intentionally
             * kept in hardware reset until this point to avoid I2C bus interference
             * during CCI_WaitForBoot. */
            GUI_Helper_InitTouch(&_GUI_Task_State, Touch_LVGL_ReadCallback);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_ERROR) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Lepton Error");

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_ERROR);
        }

        if (EventBits & GUI_TASK_CAMERA_ERROR) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Camera Error");

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_CAMERA_ERROR);
        }

        if (Timeout >= 30000) {
            esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_INIT_ERROR, NULL, 0, pdMS_TO_TICKS(500));

            break;
        }

        esp_task_wdt_reset();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
        Timeout += 100;
    } while (lv_bar_get_value(ui_SplashScreen_LoadingBar) < lv_bar_get_max_value(ui_SplashScreen_LoadingBar));

    lv_disp_load_scr(ui_Main);

    /* Process layout changes after loading new screen. */
    lv_timer_handler();

    /* Set the initial ROI FIRST to give it a size. */
    SettingsManager_GetLepton(&LeptonSettings);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SPOTMETER]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SCENE]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_AGC]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_VIDEO_FOCUS]);

    GUI_Update_Info();

    /* Initialize SD card icon based on current storage state. */
    if (MemoryManager_HasSDCard()) {
        lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);
    }

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, NULL, 0, pdMS_TO_TICKS(500));

    /* Variables for Illuminance values for the scene label.
     * Fetch all these values at the beginning to not waste CPU performance because these
     * functions aren´t simple get functions
     *      0 = Max label
     *      1 = Min label
     *      2 = Mean label
     *      3 = Crosshair label
     *      4 = Pixel temperature label
     *
     * NOTE on coordinate spaces:
     * - Labels [3] (Crosshair) and [4] (PixelTemperature) are DIRECT children of
     *   ui_Image_Thermal, so lv_obj_get_x/y() already returns image-relative coords.
     * - Labels [0..2] (Max/Min/Mean) are children of ui_Container_Main_Thermal_Scene_Statistics,
     *   which itself is a child of ui_Image_Thermal. Their lv_obj_get_x/y() is relative
     *   to the container, so the container's own offset within the image must be added.
     */
    int32_t SceneStatsContainer_x = lv_obj_get_x(ui_Container_Main_Thermal_Scene_Statistics);
    int32_t SceneStatsContainer_y = lv_obj_get_y(ui_Container_Main_Thermal_Scene_Statistics);

    int32_t SceneLabel_x0[5] = { SceneStatsContainer_x + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Max),
                                 SceneStatsContainer_x + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Min),
                                 SceneStatsContainer_x + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Mean),
                                 lv_obj_get_x(ui_Label_Main_Thermal_Crosshair),
                                 lv_obj_get_x(ui_Label_Main_Thermal_PixelTemperature)
                               };
    int32_t SceneLabel_y0[5] = { SceneStatsContainer_y + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Max),
                                 SceneStatsContainer_y + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Min),
                                 SceneStatsContainer_y + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Mean),
                                 lv_obj_get_y(ui_Label_Main_Thermal_Crosshair),
                                 lv_obj_get_y(ui_Label_Main_Thermal_PixelTemperature)
                               };
    int32_t SceneLabel_w[5] = { lv_obj_get_width(ui_Label_Main_Thermal_Scene_Max),
                                lv_obj_get_width(ui_Label_Main_Thermal_Scene_Min),
                                lv_obj_get_width(ui_Label_Main_Thermal_Scene_Mean),
                                lv_obj_get_width(ui_Label_Main_Thermal_Crosshair),
                                lv_obj_get_width(ui_Label_Main_Thermal_PixelTemperature)
                              };
    int32_t SceneLabel_h[5] = { lv_obj_get_height(ui_Label_Main_Thermal_Scene_Max),
                                lv_obj_get_height(ui_Label_Main_Thermal_Scene_Min),
                                lv_obj_get_height(ui_Label_Main_Thermal_Scene_Mean),
                                lv_obj_get_height(ui_Label_Main_Thermal_Crosshair),
                                lv_obj_get_height(ui_Label_Main_Thermal_PixelTemperature)
                              };

    while (_GUI_Task_State.isRunning) {
        EventBits_t EventBits;
        App_Lepton_FrameReady_t LeptonFrame;

        esp_task_wdt_reset();

        /* Check for new thermal frame. */
        if (xQueueReceive(App_Context->Lepton_FrameEventQueue, &LeptonFrame, 0) == pdTRUE) {
            /* During UVC streaming, skip expensive image processing to avoid
               SPI contention and watchdog timeouts. Just drain the queue. */
            if (_GUI_Task_State.isUVCStreaming) {
                ESP_LOGD(TAG, "UVC streaming active - skipping GUI frame processing");
            } else {
                uint8_t *Dst;
                uint32_t ImageWidth;
                uint32_t ImageHeight;
                char Buffer[16];
                uint32_t SceneLabelIlluminance[5] = { 0, 0, 0, 0, 0 };
                uint32_t SceneLabelCount[5] = { 0, 0, 0, 0, 0 };
                uint8_t SceneLabelAverageLuminance[5] = { 0, 0, 0, 0, 0 };

                /* Reset watchdog before image processing. */
                esp_task_wdt_reset();

                /* Scale from source (160x120) to destination (240x180) using bilinear interpolation. */
                Dst = _GUI_Task_State.ThermalCanvasBuffer;
                ImageWidth = lv_obj_get_width(ui_Image_Thermal);
                ImageHeight = lv_obj_get_height(ui_Image_Thermal);

                /* Skip if image widget not properly initialized yet. */
                if ((ImageWidth == 0) || (ImageHeight == 0)) {
                    ESP_LOGW(TAG, "Image widget not ready yet (size: %ux%u), skipping frame", ImageWidth, ImageHeight);

                    continue;
                }

                for (uint32_t x = 0; x < ImageWidth; x++) {
                    uint32_t src_x_fixed = x * ((LeptonFrame.Width - 1) << 16) / ImageWidth;
                    uint32_t xp = src_x_fixed >> 16;
                    x_lut_x0[x] = static_cast<uint8_t>(xp);
                    x_lut_x1[x] = static_cast<uint8_t>(((xp + 1) < LeptonFrame.Width) ? (xp + 1) : xp);
                    x_lut_xf[x] = static_cast<uint8_t>((src_x_fixed >> 8) & 0xFF);
                }

                for (uint32_t y = 0; y < ImageHeight; y++) {
                    /* Reset watchdog every 20 rows to prevent timeout during image processing. */
                    if ((y % 20) == 0) {
                        esp_task_wdt_reset();
                    }

                    uint32_t src_y_fixed = y * ((LeptonFrame.Height - 1) << 16) / ImageHeight;
                    uint32_t y0 = src_y_fixed >> 16;
                    uint32_t y1 = ((y0 + 1) < LeptonFrame.Height) ? (y0 + 1) : y0;
                    uint32_t y_frac = (src_y_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                    uint32_t y_inv = 256 - y_frac;

                    for (uint32_t x = 0; x < ImageWidth; x++) {
                        /* Use precomputed x LUT - avoids 1 multiply + 1 divide per pixel per row.
                         * x_inv is computed inline (not cached) to avoid uint8_t overflow when x_frac==0. */
                        uint32_t x0 = x_lut_x0[x];
                        uint32_t x1 = x_lut_x1[x];
                        uint32_t x_frac = x_lut_xf[x];
                        uint32_t x_inv = 256u - x_frac;

                        /* Get the four surrounding pixels. */
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
                        /* Map buffer coordinates to display coordinates for 180° rotated image:
                         * x: x_rot = ImageWidth - 1 - x  (horizontal mirror)
                         * y: y_rot = ImageHeight - 1 - y  (vertical mirror)
                         * Labels use display coordinates, so both axes must be inverted.
                         */
                        uint32_t x_rot = ImageWidth - 1 - x;
                        uint32_t y_rot = ImageHeight - 1 - y;

                        /* Max label */
                        if ((x_rot >= SceneLabel_x0[0]) &&
                            (x_rot < (SceneLabel_x0[0] + SceneLabel_w[0])) &&
                            (y_rot >= SceneLabel_y0[0]) &&
                            (y_rot < (SceneLabel_y0[0] + SceneLabel_h[0]))) {
                            /* BT.601 luma in integer: (77*R + 150*G + 29*B) >> 8 (coefficients sum to 256) */
                            SceneLabelIlluminance[0] += ((77u * r) + (150u * g) + (29u * b)) >> 8u;
                            SceneLabelCount[0]++;
                        }

                        /* Min label */
                        if ((x_rot >= SceneLabel_x0[1]) &&
                            (x_rot < (SceneLabel_x0[1] + SceneLabel_w[1])) &&
                            (y_rot >= SceneLabel_y0[1]) &&
                            (y_rot < (SceneLabel_y0[1] + SceneLabel_h[1]))) {
                            SceneLabelIlluminance[1] += ((77u * r) + (150u * g) + (29u * b)) >> 8u;
                            SceneLabelCount[1]++;
                        }

                        /* Mean label */
                        if ((x_rot >= SceneLabel_x0[2]) &&
                            (x_rot < (SceneLabel_x0[2] + SceneLabel_w[2])) &&
                            (y_rot >= SceneLabel_y0[2]) &&
                            (y_rot < (SceneLabel_y0[2] + SceneLabel_h[2]))) {
                            SceneLabelIlluminance[2] += ((77u * r) + (150u * g) + (29u * b)) >> 8u;
                            SceneLabelCount[2]++;
                        }

                        /* Crosshair label */
                        if ((x_rot >= SceneLabel_x0[3]) &&
                            (x_rot < (SceneLabel_x0[3] + SceneLabel_w[3])) &&
                            (y_rot >= SceneLabel_y0[3]) &&
                            (y_rot < (SceneLabel_y0[3] + SceneLabel_h[3]))) {
                            SceneLabelIlluminance[3] += ((77u * r) + (150u * g) + (29u * b)) >> 8u;
                            SceneLabelCount[3]++;
                        }

                        /* Pixel temperature label */
                        if ((x_rot >= SceneLabel_x0[4]) &&
                            (x_rot < (SceneLabel_x0[4] + SceneLabel_w[4])) &&
                            (y_rot >= SceneLabel_y0[4]) &&
                            (y_rot < (SceneLabel_y0[4] + SceneLabel_h[4]))) {
                            SceneLabelIlluminance[4] += ((77u * r) + (150u * g) + (29u * b)) >> 8u;
                            SceneLabelCount[4]++;
                        }

                        uint32_t dst_idx = (y * ImageWidth) + x;

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
                lv_obj_set_style_text_color(ui_Label_Main_Thermal_Crosshair,
                                            (SceneLabelAverageLuminance[3] > 128) ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
                lv_obj_set_style_text_color(ui_Label_Main_Thermal_PixelTemperature,
                                            (SceneLabelAverageLuminance[4] > 128) ? lv_color_black() : lv_color_white(), LV_PART_MAIN);

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
                         ImageWidth, ImageHeight);

                /* Save frame if requested */
                if (_GUI_Task_State.SaveNextFrameRequested) {
                    App_Lepton_FrameReady_t SaveFrame;

                    _GUI_Task_State.SaveNextFrameRequested = false;

                    /* Prepare frame data for save task (using scaled RGB565 buffer) */
                    SaveFrame.Buffer = _GUI_Task_State.ThermalCanvasBuffer;  /* Use scaled display buffer */
                    SaveFrame.Width = ImageWidth;
                    SaveFrame.Height = ImageHeight;

                    /* RGB565 = 2 bytes per pixel */
                    SaveFrame.Channels = 2;
                    SaveFrame.Min = LeptonFrame.Min;
                    SaveFrame.Max = LeptonFrame.Max;

                    if (xQueueSend(_GUI_Task_State.ImageSaveQueue, &SaveFrame, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "Image save queue full, skipping save");

                        esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_THERMAL_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));
                    }
                }

                /* Update network frame for server streaming if server is running */
                if (Server_IsRunning()) {
                    if (xSemaphoreTake(_GUI_Task_State.NetworkFrame.Mutex, 0) == pdTRUE) {
                        /* Convert scaled RGB565 buffer to RGB888 for network transmission */
                        uint8_t *rgb888_dst = _GUI_Task_State.NetworkRGBBuffer;

                        /* Reset watchdog before RGB conversion */
                        esp_task_wdt_reset();

                        for (uint32_t i = 0; i < (ImageWidth * ImageHeight); i++) {
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

                        _GUI_Task_State.NetworkFrame.Buffer = _GUI_Task_State.NetworkRGBBuffer;
                        _GUI_Task_State.NetworkFrame.Width = ImageWidth;
                        _GUI_Task_State.NetworkFrame.Height = ImageHeight;
                        _GUI_Task_State.NetworkFrame.Timestamp = esp_timer_get_time() / 1000;

                        xSemaphoreGive(_GUI_Task_State.NetworkFrame.Mutex);

                        /* Reset watchdog after RGB conversion */
                        esp_task_wdt_reset();
                    }

                    Server_NotifyClients();
                }
            }
        }

        /* Process the recieved system events */
        EventBits = xEventGroupGetBits(_GUI_Task_State.EventGroup);
        if (EventBits & GUI_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _GUI_Task_State.isRunning = false;

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & GUI_TASK_BATTERY_STATUS_CHANGED) {
            char Buffer[16];

            snprintf(Buffer, sizeof(Buffer), "%d%%", _GUI_Task_State.BatteryInfo.Percentage);

            if (_GUI_Task_State.BatteryInfo.Percentage == 100) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_FULL " %d%%", _GUI_Task_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0x00FF00), 0);
            } else if (_GUI_Task_State.BatteryInfo.Percentage >= 75) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_3 " %d%%", _GUI_Task_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0x00FF00), 0);
            } else if (_GUI_Task_State.BatteryInfo.Percentage >= 50) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_2 " %d%%", _GUI_Task_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFFFF00), 0);
            } else if (_GUI_Task_State.BatteryInfo.Percentage >= 25) {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_1 " %d%%", _GUI_Task_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFFFF00), 0);
            } else {
                snprintf(Buffer, sizeof(Buffer), LV_SYMBOL_BATTERY_EMPTY " %d%%", _GUI_Task_State.BatteryInfo.Percentage);
                lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining, lv_color_hex(0xFF0000), 0);
            }

            lv_bar_set_value(ui_Info_Battery_Bar, _GUI_Task_State.BatteryInfo.Percentage, LV_ANIM_OFF);
            lv_label_set_text(ui_Label_Main_Battery_Remaining, Buffer);
            lv_label_set_text(ui_Label_Info_Battery_Remaining, Buffer);

            if (_GUI_Task_State.BatteryInfo.Charging) {
                lv_label_set_text(ui_Label_Info_Battery_Status, "Charging");
            } else {
                lv_label_set_text(ui_Label_Info_Battery_Status, "Not charging");
            }

            snprintf(Buffer, sizeof(Buffer), "%d mV", _GUI_Task_State.BatteryInfo.Voltage);
            lv_label_set_text(ui_Label_Info_Battery_Voltage, Buffer);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_BATTERY_STATUS_CHANGED);
        }

        if (EventBits & GUI_TASK_WIFI_CONNECTION_STATE_CHANGED) {
            if (_GUI_Task_State.WiFiConnected) {
                char Buffer[32];

                memset(Buffer, 0, sizeof(Buffer));
                snprintf(Buffer, sizeof(Buffer), "IP: %lu.%lu.%lu.%lu",
                         (_GUI_Task_State.IP_Info.IP >> 0) & 0xFF,
                         (_GUI_Task_State.IP_Info.IP >> 8) & 0xFF,
                         (_GUI_Task_State.IP_Info.IP >> 16) & 0xFF,
                         (_GUI_Task_State.IP_Info.IP >> 24) & 0xFF);

                /* Skip "IP: " prefix for label */
                lv_label_set_text(ui_Label_Info_IP, &Buffer[4]);
                lv_label_set_text(ui_settings_wifi_status_label, &Buffer[4]);
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0x00FF00), LV_PART_MAIN);

                MessageBox_Show(Buffer, 5);

                /* Disable WiFi buttons in the settings menu */
                lv_obj_remove_flag(ui_settings_wifi_connect_btn, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_label_set_text(ui_Label_Info_IP, "Not connected");
                lv_label_set_text(ui_settings_wifi_status_label, "Not connected");
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);

                /* Disable WiFi buttons in the settings menu */
                lv_obj_add_flag(ui_settings_wifi_connect_btn, LV_OBJ_FLAG_CLICKABLE);
            }

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_PROVISIONING_STATE_CHANGED) {
            if ((_GUI_Task_State.WiFiConnected == false) && _GUI_Task_State.ProvisioningActive) {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF8800), LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_SD_CARD_STATE_CHANGED) {
            if (_GUI_Task_State.CardPresent) {
                if (MemoryManager_HasSDCard()) {
                    /* MemoryManager already auto-mounted the SD card at init - just confirm green */
                    lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);

                    ESP_LOGD(TAG, "SD card already mounted by MemoryManager");
                } else {
                    /* Card present but not yet mounted: show orange while mounting */
                    lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0xFF8800), LV_PART_MAIN);

                    if (MemoryManager_SwitchToSDCard() == ESP_OK) {
                        /* Successfully mounted: show green */
                        lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);

                        ESP_LOGD(TAG, "Storage switched to SD card");
                    } else {
                        /* Mount failed: stay orange (card present but not usable) */
                        ESP_LOGW(TAG, "SD card detected but mount failed - keeping orange");
                    }
                }
            } else {
                /* Card removed: switch back to internal only if we are currently on SD */
                if (MemoryManager_HasSDCard()) {
                    MemoryManager_SwitchToInternal();

                    ESP_LOGD(TAG, "Storage switched back to internal flash");
                }

                lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_SD_CARD_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_LEPTON_UPTIME_READY) {
            char Buffer[32];
            uint32_t Uptime;

            Uptime = _GUI_Task_State.LeptonUptime / 1000;

            snprintf(Buffer, sizeof(Buffer), "%02lu:%02lu:%02lu", Uptime / 3600, (Uptime % 3600) / 60, Uptime % 60);
            lv_label_set_text(ui_Label_Info_Lepton_Uptime, Buffer);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_UPTIME_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_TEMP_READY) {
            char Buffer[32];

            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUI_Task_State.LeptonTemperatures.FPA);
            lv_label_set_text(ui_Label_Info_Lepton_FPA, Buffer);
            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUI_Task_State.LeptonTemperatures.AUX);
            lv_label_set_text(ui_Label_Info_Lepton_AUX, Buffer);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_TEMP_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY) {
            char Buffer[16];

            snprintf(Buffer, sizeof(Buffer), "%.2f °C", _GUI_Task_State.SpotTemperature);
            lv_label_set_text(ui_Label_Main_Thermal_PixelTemperature, Buffer);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_SCENE_STATISTICS_READY) {
            char Buffer[16];
            float Temp;

            Temp = _GUI_Task_State.ROIResult.Max;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Max, Buffer);

            Temp = _GUI_Task_State.ROIResult.Min;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Min, Buffer);

            Temp = _GUI_Task_State.ROIResult.Mean ;
            snprintf(Buffer, sizeof(Buffer), "%.1f °C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Mean, Buffer);

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_LEPTON_SCENE_STATISTICS_READY);
        }

        if (EventBits & GUI_TASK_UVC_STREAMING_STATE_CHANGED) {
            if (_GUI_Task_State.isUVCStreaming) {
                /* Clear thermal canvas to black */
                memset(_GUI_Task_State.ThermalCanvasBuffer, 0x00, 240 * 180 * 2);
                lv_obj_invalidate(ui_Image_Thermal);

                /* Show UVC overlay */
                lv_obj_remove_flag(_GUI_Task_State.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);

                /* Hide ROI rectangles */
                lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);

                /* Hide temperature labels */
                lv_obj_add_flag(ui_Label_Main_Thermal_PixelTemperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);

                /* Hide temperature scale labels */
                lv_obj_add_flag(ui_Label_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
            } else {
                /* Hide UVC overlay */
                lv_obj_add_flag(_GUI_Task_State.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);

                /* Restore ROI rectangles */
                lv_obj_remove_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);

                /* Restore temperature labels */
                lv_obj_remove_flag(ui_Label_Main_Thermal_PixelTemperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);

                /* Restore temperature scale labels */
                lv_obj_remove_flag(ui_Label_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
            }

            xEventGroupClearBits(_GUI_Task_State.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);
        }

        _lock_acquire(&_GUI_Task_State.LVGL_API_Lock);
        lv_timer_handler();
        _lock_release(&_GUI_Task_State.LVGL_API_Lock);

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    _GUI_Task_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t GUI_Task_Init(void)
{
    BaseType_t Error;
    uint32_t Caps;

    if (_GUI_Task_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_ERROR_CHECK(GUI_Helper_Init(&_GUI_Task_State, Touch_LVGL_ReadCallback));
    ESP_ERROR_CHECK(GUI_Helper_InitKeypad(&_GUI_Task_State, Keypad_LVGL_ReadCallback));

    ui_init();

#ifdef CONFIG_SPIRAM
    Caps = MALLOC_CAP_SPIRAM;
#else
    Caps = 0;
#endif

    _GUI_Task_State.ThermalCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(240 * 180 * 2, Caps));
    _GUI_Task_State.GradientCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(20 * 180 * 2, Caps));
    _GUI_Task_State.NetworkRGBBuffer = static_cast<uint8_t *>(heap_caps_malloc(240 * 180 * 3, Caps));

    if (_GUI_Task_State.ThermalCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate thermal canvas buffer!");

        return ESP_ERR_NO_MEM;
    }

    if (_GUI_Task_State.GradientCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate gradient canvas buffer!");

        heap_caps_free(_GUI_Task_State.ThermalCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUI_Task_State.NetworkRGBBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network RGB buffer!");

        heap_caps_free(_GUI_Task_State.ThermalCanvasBuffer);
        heap_caps_free(_GUI_Task_State.GradientCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    _GUI_Task_State.ImageSaveQueue = xQueueCreate(1, sizeof(App_Lepton_FrameReady_t));
    if (_GUI_Task_State.ImageSaveQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create image save queue!");

        heap_caps_free(_GUI_Task_State.ThermalCanvasBuffer);
        heap_caps_free(_GUI_Task_State.GradientCanvasBuffer);
        heap_caps_free(_GUI_Task_State.NetworkRGBBuffer);

        return ESP_ERR_NO_MEM;
    }

    Error = xTaskCreatePinnedToCore(Task_ImageSave, "Task_ImgSave", 8192, NULL, CONFIG_GUI_TASK_PRIO - 1,
                                    &_GUI_Task_State.ImageSaveTaskHandle, 1);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create image save task!");

        vQueueDelete(_GUI_Task_State.ImageSaveQueue);

        heap_caps_free(_GUI_Task_State.ThermalCanvasBuffer);
        heap_caps_free(_GUI_Task_State.GradientCanvasBuffer);
        heap_caps_free(_GUI_Task_State.NetworkRGBBuffer);

        _GUI_Task_State.ImageSaveQueue = NULL;
        _GUI_Task_State.ThermalCanvasBuffer = NULL;
        _GUI_Task_State.GradientCanvasBuffer = NULL;
        _GUI_Task_State.NetworkRGBBuffer = NULL;

        return ESP_FAIL;
    }

    /* Initialize buffers with black pixels (RGB565 = 0x0000) */
    memset(_GUI_Task_State.ThermalCanvasBuffer, 0x00, 240 * 180 * 2);
    memset(_GUI_Task_State.GradientCanvasBuffer, 0x00, 20 * 180 * 2);

    /* Now configure the image descriptors with allocated buffers */
    _GUI_Task_State.ThermalImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUI_Task_State.ThermalImageDescriptor.header.w = 240;
    _GUI_Task_State.ThermalImageDescriptor.header.h = 180;
    _GUI_Task_State.ThermalImageDescriptor.data = _GUI_Task_State.ThermalCanvasBuffer;
    _GUI_Task_State.ThermalImageDescriptor.data_size = 240 * 180 * 2;

    _GUI_Task_State.GradientImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUI_Task_State.GradientImageDescriptor.header.w = 20;
    _GUI_Task_State.GradientImageDescriptor.header.h = 180;
    _GUI_Task_State.GradientImageDescriptor.data = _GUI_Task_State.GradientCanvasBuffer;
    _GUI_Task_State.GradientImageDescriptor.data_size = 20 * 180 * 2;

    UI_Canvas_AddTempGradient();

    lv_img_set_src(ui_Image_Thermal, &_GUI_Task_State.ThermalImageDescriptor);
    lv_img_set_src(ui_Image_Gradient, &_GUI_Task_State.GradientImageDescriptor);

    /* Create UVC streaming overlay label (hidden by default) */
    _GUI_Task_State.UVCOverlayLabel = lv_label_create(ui_Container_Main_Thermal);
    lv_label_set_text(_GUI_Task_State.UVCOverlayLabel, "USB Video Mode");
    lv_obj_set_align(_GUI_Task_State.UVCOverlayLabel, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(_GUI_Task_State.UVCOverlayLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(_GUI_Task_State.UVCOverlayLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_flag(_GUI_Task_State.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);
    _GUI_Task_State.isUVCStreaming = false;

    /* Initialize network frame for server streaming */
    _GUI_Task_State.NetworkFrame.Mutex = xSemaphoreCreateMutex();
    if (_GUI_Task_State.NetworkFrame.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create NetworkFrame mutex!");

        heap_caps_free(_GUI_Task_State.ThermalCanvasBuffer);
        heap_caps_free(_GUI_Task_State.GradientCanvasBuffer);
        heap_caps_free(_GUI_Task_State.NetworkRGBBuffer);

        return ESP_ERR_NO_MEM;
    }

    esp_event_handler_register(DEVICES_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler, NULL);
    esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
    esp_event_handler_register(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler, NULL);
    esp_event_handler_register(DEVICES_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Task_Event_Handler, NULL);
    esp_event_handler_register(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(LEPTON_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Task_Event_Handler, NULL);
    esp_event_handler_register(CAMERA_EVENTS, ESP_EVENT_ANY_ID, on_Camera_Task_Event_Handler, NULL);

    _GUI_Task_State.SaveNextFrameRequested = false;
    _GUI_Task_State.isInitialized = true;

    return ESP_OK;
}

void GUI_Task_Deinit(void)
{
    if (_GUI_Task_State.isInitialized == false) {
        return;
    }

    esp_event_handler_unregister(DEVICES_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler);
    esp_event_handler_unregister(DEVICES_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Task_Event_Handler);
    esp_event_handler_unregister(GUI_TASK_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(LEPTON_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Task_Event_Handler);
    esp_event_handler_unregister(CAMERA_EVENTS, ESP_EVENT_ANY_ID, on_Camera_Task_Event_Handler);

    ui_destroy();

    GUI_Helper_Deinit(&_GUI_Task_State);

    if (_GUI_Task_State.NetworkFrame.Mutex != NULL) {
        vSemaphoreDelete(_GUI_Task_State.NetworkFrame.Mutex);
        _GUI_Task_State.NetworkFrame.Mutex = NULL;
    }

    _GUI_Task_State.Display = NULL;
    _GUI_Task_State.isInitialized = false;
}

esp_err_t GUI_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_GUI_Task_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_GUI_Task_State.isRunning) {
        ESP_LOGW(TAG, "Task already running");

        return ESP_OK;
    }

    _GUI_Task_State.isRunning = true;

    ESP_LOGD(TAG, "Starting GUI Task");

    Error = xTaskCreatePinnedToCore(Task_GUI, "Task_GUI", CONFIG_GUI_TASK_STACKSIZE, p_AppContext, CONFIG_GUI_TASK_PRIO,
                                    &_GUI_Task_State.TaskHandle, CONFIG_GUI_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task: 0x%X!", Error);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t GUI_Task_Stop(void)
{
    if (_GUI_Task_State.isRunning == false) {
        return ESP_OK;
    }

    xEventGroupSetBits(_GUI_Task_State.EventGroup, GUI_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool GUI_Task_IsRunning(void)
{
    return _GUI_Task_State.isRunning;
}

esp_err_t GUI_SaveThermalImage(void)
{
    /* Check if filesystem is locked (USB active) */
    if (MemoryManager_IsFilesystemLocked()) {
        ESP_LOGW(TAG, "Cannot save image - USB mode active!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Set flag to trigger save on next frame update */
    _GUI_Task_State.SaveNextFrameRequested = true;
    ESP_LOGD(TAG, "Image save requested - will capture next frame");

    return ESP_OK;
}