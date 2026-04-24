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

#include "lepton.h"

#define UI_IMAGE_CANVAS_WIDTH                   240
#define UI_IMAGE_CANVAS_HEIGHT                  180
#define UI_GRADIENT_CANVAS_WIDTH                20

#define CROSSHAIR_STEP_PX                       8
#define CROSSHAIR_INITIAL_DELAY_MS              30
#define CROSSHAIR_AUTOREPEAT_DELAY_MS           400
#define CROSSHAIR_AUTOREPEAT_PERIOD_MS          150

/* Fixed container dimensions taken from the UI export (ui_Main.c).
 * Using constants avoids calling lv_obj_get_width/height on a potentially
 * hidden widget whose coords may not be up-to-date at indev-callback time. */
#define CROSSHAIR_CONTAINER_W                   100
#define CROSSHAIR_CONTAINER_H                   50

ESP_EVENT_DEFINE_BASE(GUI_TASK_EVENTS);

GUI_Task_State_t _GUITaskState;

static const char *TAG = "GUI-Task";

/* Forward declaration */
static void GUI_Task_UpdateGradient(void);

/** @brief                  Event handler for the Lepton task events to receive updates when Lepton events are triggered (e.G., new frame ready, camera errors).
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
            memcpy(&_GUITaskState.LeptonDeviceInfo, static_cast<const App_Lepton_Device_t *>(p_Data),
                   sizeof(App_Lepton_Device_t));

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_READY);

            break;
        }
        case LEPTON_TASK_EVENT_CAMERA_ERROR: {
            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_ERROR);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_FPA_AUX_TEMP: {
            memcpy(&_GUITaskState.LeptonTemperatures, p_Data, sizeof(App_Lepton_Temperatures_t));

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_TEMPERATURE_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_SCENE_STATISTICS: {
            memcpy(&_GUITaskState.ROIResult, p_Data, sizeof(App_Lepton_ROI_Result_t));

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_SCENE_STATISTICS_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_UPTIME: {
            memcpy(&_GUITaskState.LeptonUptime, p_Data, sizeof(uint32_t));

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_UPTIME_READY);

            break;
        }
        case LEPTON_TASK_EVENT_RESPONSE_PIXEL_TEMP: {
            memcpy(&_GUITaskState.SpotTemperature, p_Data, sizeof(float));

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY);

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
        case CAMERA_TASK_EVENT_INIT_COMPLETE: {
            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_READY);

            break;
        }
        case CAMERA_TASK_EVENT_INIT_FAILED: {
            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_ERROR);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled Camera event ID: 0x%X", ID);

            break;
        }
    }
}

/** @brief                  Event handler for settings events to react on changes relevant to the GUI
 *                          (e.g., palette change requiring the gradient canvas to be redrawn).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Settings_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Settings event received: ID=%d", ID);

    switch (ID) {
        case SETTINGS_EVENT_LEPTON_CHANGED: {
            SettingsManager_ChangeNotification_t Changed;

            memcpy(&Changed, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            if (Changed.ID == SETTINGS_ID_LEPTON_PALETTE) {
                ESP_LOGD(TAG, "Palette changed to index: %u — scheduling gradient redraw",
                         static_cast<unsigned int>(Changed.Value));

                xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_GRADIENT_REDRAW_REQUIRED);
            }

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief                  Event handler for GUI task events (e.G., image save completion).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_GUI_Task_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI task event received: ID=%d", ID);

    switch (ID) {
        case GUI_TASK_EVENT_IMAGE_SAVED: {
            ESP_LOGD(TAG, "Image saved successfully");

            xEventGroupSetBits(_GUITaskState.EventGroup,
                               GUI_TASK_IMAGE_SAVE_COMPLETED | GUI_TASK_SCREEN_REFRESH_REQUIRED);

            break;
        }
        case GUI_TASK_EVENT_IMAGE_SAVE_FAILED: {
            ESP_LOGE(TAG, "Image save failed");

            APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_FAIL);

            xEventGroupSetBits(_GUITaskState.EventGroup,
                               GUI_TASK_IMAGE_SAVE_FAILED_BIT | GUI_TASK_SCREEN_REFRESH_REQUIRED);

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
            memcpy(&_GUITaskState.BatteryInfo, p_Data, sizeof(App_Devices_Battery_t));

            ESP_LOGD(TAG, "Battery status updated: Voltage=%dmV, Percentage=%d%%, Charging=%s",
                     _GUITaskState.BatteryInfo.Voltage, _GUITaskState.BatteryInfo.Percentage,
                     _GUITaskState.BatteryInfo.Charging ? "Yes" : "No");

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_BATTERY_STATUS_CHANGED);

            break;
        }
        case DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE: {
            memcpy(&_GUITaskState.TemperatureInfo, p_Data, sizeof(App_Devices_Temperature_t));

            ESP_LOGD(TAG, "Room temperature updated: %.1f \xC2\xB0""C", _GUITaskState.TemperatureInfo.TempSensor);

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_TEMPERATURE_SENSOR_READY);

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
            _GUITaskState.CardPresent = *static_cast<const bool *>(p_Data);

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_SD_CARD_STATE_CHANGED);

            break;
        }
    }
}

/** @brief                  Event handler for the Network events to receive updates when network events are triggered (e.G., WiFi connection changes).
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Network event received: ID=%d", ID);

    switch (ID) {
        case NETWORK_EVENT_WIFI_GOT_IP: {
            memcpy(&_GUITaskState.IP_Info, p_Data, sizeof(Network_IP_Info_t));
            _GUITaskState.WiFiConnected = true;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            _GUITaskState.WiFiConnected = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STARTED: {
            _GUITaskState.ProvisioningActive = true;
            _GUITaskState.WiFiConnected = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STOPPED: {
            _GUITaskState.ProvisioningActive = false;
            _GUITaskState.WiFiConnected = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            _GUITaskState.ProvisioningActive = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            _GUITaskState.ProvisioningActive = false;
            _GUITaskState.WiFiConnected = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_SERVER_STARTED: {
            ESP_LOGD(TAG, "Network frame registered with server");

            Server_SetThermalFrame(&_GUITaskState.NetworkFrame);

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
            _GUITaskState.IsUVCStreaming = true;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);

            break;
        }
        case USB_EVENT_UVC_STREAMING_STOP: {
            _GUITaskState.IsUVCStreaming = false;

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);

            break;
        }
        case USB_EVENT_UNINITIALIZED: {
            if (_GUITaskState.IsUVCStreaming) {
                _GUITaskState.IsUVCStreaming = false;

                xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);
            }

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief          LVGL display flush callback.
 *                  Writes the LVGL render buffer to the ILI9341 LCD via SPI.
 *                  Uses a non-blocking try-lock on the SPI bus mutex: when Task_ImageSave
 *                  holds the bus for SD card writes, the flush is skipped and LVGL is
 *                  notified immediately to prevent blocking Task_GUI and triggering the WDT.
 *  @param p_Disp   LVGL display handle
 *  @param p_Area   Dirty rectangle to update
 *  @param p_PxMap  Pixel data to flush
 */
static void GUI_LCD_Flush_CB(lv_display_t *p_Disp, const lv_area_t *p_Area, uint8_t *p_PxMap)
{
    int OffsetX1 = p_Area->x1;
    int OffsetX2 = p_Area->x2;
    int OffsetY1 = p_Area->y1;
    int OffsetY2 = p_Area->y2;

    /* If Task_ImageSave currently holds the SPI bus for SD writes,
     * skip this LCD update and signal LVGL immediately so Task_GUI never blocks and can
     * continue resetting the task watchdog. The display simply shows the previous frame
     * until the save completes.
     */
    if (xSemaphoreTake(_GUITaskState.SpiMutex, 0) == pdFALSE) {
        lv_display_flush_ready(p_Disp);

        return;
    }

    esp_lcd_panel_draw_bitmap(static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(p_Disp)), OffsetX1, OffsetY1,
                              OffsetX2 + 1, OffsetY2 + 1, p_PxMap);
    xSemaphoreGive(_GUITaskState.SpiMutex);
}

/** @brief Update the information screen labels.
 */
static void GUI_Update_Info(void)
{
    uint8_t MAC[6];
    char Buffer[32];

    esp_efuse_mac_get_default(MAC);
    snprintf(Buffer, sizeof(Buffer), "%02X:%02X:%02X:%02X:%02X:%02X", MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);

    lv_label_set_text(ui_Label_Info_Lepton_Serial, _GUITaskState.LeptonDeviceInfo.SerialNumber);
    lv_label_set_text(ui_Label_Info_Lepton_Part, _GUITaskState.LeptonDeviceInfo.PartNumber);
    lv_label_set_text(ui_Label_Info_Lepton_GPP_Revision, _GUITaskState.LeptonDeviceInfo.SoftwareRevision.GPP_Revision);
    lv_label_set_text(ui_Label_Info_Lepton_DSP_Revision, _GUITaskState.LeptonDeviceInfo.SoftwareRevision.DSP_Revision);
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

    /* Update visual rectangle on display (convert Lepton coords to display coords). */
    ESP_LOGD(TAG, "Updating ROI rectangle - Start: (%ld,%ld), End: (%ld,%ld), Size: %ldx%ld",
             ROI.x, ROI.y, ROI.x + ROI.w, ROI.y + ROI.h, ROI.w, ROI.h);

    DisplayWidth = lv_obj_get_width(ui_Image_Main_Image);
    DisplayHeight = lv_obj_get_height(ui_Image_Main_Image);

    int32_t DispX = (ROI.x * DisplayWidth) / 160;
    int32_t DispY = (ROI.y * DisplayHeight) / 120;
    int32_t DispW = (ROI.w * DisplayWidth) / 160;
    int32_t DispH = (ROI.h * DisplayHeight) / 120;

    /* Map ROI type to its LVGL overlay widget and apply the computed position/size. */
    lv_obj_t *const ROI_WIDGETS[] = {
        ui_Image_Main_Thermal_Spotmeter_ROI,
        ui_Image_Main_Thermal_Scene_ROI,
        ui_Image_Main_Thermal_AGC_ROI,
        ui_Image_Main_Thermal_Video_Focus_ROI,
    };

    if (static_cast<size_t>(ROI.Type) >= (sizeof(ROI_WIDGETS) / sizeof(ROI_WIDGETS[0]))) {
        ESP_LOGW(TAG, "Invalid GUI ROI type: 0x%X", ROI.Type);

        return;
    }

    lv_obj_t *p_Widget = ROI_WIDGETS[ROI.Type];
    if (p_Widget != NULL) {
        lv_obj_set_align(p_Widget, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(p_Widget, DispX, DispY);
        lv_obj_set_size(p_Widget, DispW, DispH);
    }

    SettingsManager_GetLepton(&SettingsLepton);

    /* Check if an update is required. */
    if ((SettingsLepton.ROI[ROI.Type].x == ROI.x) &&
        (SettingsLepton.ROI[ROI.Type].y == ROI.y) &&
        (SettingsLepton.ROI[ROI.Type].w == ROI.w) &&
        (SettingsLepton.ROI[ROI.Type].h == ROI.h)) {
        ESP_LOGD(TAG, "ROI unchanged, not updating NVS");

        return;
    }

    /* Copy the new ROI in the existing settings structure. */
    memcpy(&SettingsLepton.ROI[ROI.Type], &ROI, sizeof(Settings_ROI_t));

    /* Save the new ROI to NVS. */
    // TODO: Fix me. Causes crashes during boot
    //SettingsManager_UpdateLepton(&SettingsLepton);
    SettingsManager_Save();

    /* The Lepton task needs the ROI with the Lepton coordinates. */
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

/** @brief          Create temperature gradient canvas for palette visualization.
 *                  Generates a vertical gradient from hot (top of canvas, palette index 255)
 *                  to cold (bottom of canvas, palette index 0) using fictive temperature values
 *                  mapped linearly across the full 256-entry palette LUT.
 *  @param p_Palette Pointer to the 256-entry RGB888 palette LUT to use for the gradient.
 */
static void UI_Canvas_AddTempGradient(const uint8_t (*p_Palette)[3])
{
    /* Generate gradient pixel by pixel. */
    uint16_t *Buffer = reinterpret_cast<uint16_t *>(_GUITaskState.GradientCanvasBuffer);

    for (uint32_t y = 0; y < _GUITaskState.GradientImageDescriptor.header.h; y++) {
        uint32_t Index;

        /* Map y position to palette index using fictive temperature values:
         * top (y=0)   -> index 255 (hottest)
         * bottom (y=H-1) -> index 0 (coldest)
         */
        Index = 255 - (y * 255 / (_GUITaskState.GradientImageDescriptor.header.h - 1));

        /* Get RGB888 values from the active palette LUT. */
        uint8_t R8 = p_Palette[Index][0];
        uint8_t G8 = p_Palette[Index][1];
        uint8_t B8 = p_Palette[Index][2];

        /* Convert RGB888 to RGB565. */
        uint16_t R5 = (R8 >> 3) & 0x1F;
        uint16_t G6 = (G8 >> 2) & 0x3F;
        uint16_t B5 = (B8 >> 3) & 0x1F;

        /* Fill entire row with same color. */
        for (uint32_t x = 0; x < _GUITaskState.GradientImageDescriptor.header.w; x++) {
            Buffer[y * _GUITaskState.GradientImageDescriptor.header.w + x] = (R5 << 11) | (G6 << 5) | B5;
        }
    }
}

/** @brief  Redraw the gradient canvas using the palette currently stored in settings
 *          and invalidate the LVGL image widget so the new colours are displayed.
 *  @note   Must be called from the GUI task only (not thread-safe).
 */
static void GUI_Task_UpdateGradient(void)
{
    Settings_Lepton_t LeptonSettings;
    uint8_t PaletteIdx;

    SettingsManager_GetLepton(&LeptonSettings);
    PaletteIdx = LeptonSettings.Palette < LEPTON_PALETTE_COUNT ? LeptonSettings.Palette : 0U;

    UI_Canvas_AddTempGradient(Lepton_Palette_Table[PaletteIdx]);
    lv_obj_invalidate(ui_Image_Main_Gradient);
}

/** @brief          Reposition ui_Label_Main_Thermal_PixelTemperature relative to the crosshair
 *                  centre so the label text always stays within the visible thermal canvas.
 *                  The label is given a fixed width equal to the container width so that its
 *                  bounding box is always confined to [0, CROSSHAIR_CONTAINER_W] within the
 *                  container — regardless of the actual text length.  Text visibility near
 *                  the image edges is achieved by changing the text-align style rather than
 *                  by shifting the label's x position:
 *                    - Default:         LV_TEXT_ALIGN_CENTER, dy = –LABEL_Y_OFFSET (above).
 *                    - Near top edge:   dy = +LABEL_Y_OFFSET (below); text-align unchanged.
 *                    - Near left edge:  LV_TEXT_ALIGN_RIGHT (text hugs the right/visible side).
 *                    - Near right edge: LV_TEXT_ALIGN_LEFT  (text hugs the left/visible side).
 *  @note           Because the label bounding box never leaves the container, no dx shift or
 *                  clamp arithmetic is needed and clipping artefacts are impossible.
 *                  Must be called from the GUI task only (not thread-safe).
 *                  Flip state is stored in static locals; hysteresis prevents flickering at
 *                  the threshold positions.
 *  @param CX       Crosshair centre X in thermal canvas pixels [0 .. UI_IMAGE_CANVAS_WIDTH-1].
 *  @param CY       Crosshair centre Y in thermal canvas pixels [0 .. UI_IMAGE_CANVAS_HEIGHT-1].
 */
static void GUI_UpdateTempLabelPosition(int32_t CX, int32_t CY)
{
    int32_t dy;
    int32_t TopWhenAbove;
    lv_text_align_t TextAlign;
    static bool Initialized = false;    /**< true once the label width has been set to a fixed value. */
    static bool FlippedY = false;       /**< true while label is shown below the crosshair. */
    static bool FlippedLeft = false;    /**< true while text is right-aligned (near left image edge). */
    static bool FlippedRight = false;   /**< true while text is left-aligned (near right image edge). */

    /* Nominal y offset: label centre is this many pixels above/below the crosshair centre. */
    static const int32_t LABEL_Y_OFFSET = 15;
    /* Hysteresis: crosshair must move this far past the flip threshold before reverting. */
    static const int32_t HYSTERESIS = 5;
    /* Fallback label height before LVGL has performed the first layout pass. */
    static const int32_t LBL_H_FALLBACK = 15;

    int32_t ImageW = static_cast<int32_t>(UI_IMAGE_CANVAS_WIDTH);
    int32_t HalfContainerW = CROSSHAIR_CONTAINER_W / 2;

    /* Give the label a fixed pixel width equal to the container width once.
     * With a fixed-width label centred at dx=0, the label spans [0, CROSSHAIR_CONTAINER_W]
     * within the container — it is always inside the container's clip region and can never
     * be partially cut off.  All horizontal "positioning" is handled by text-align alone. */
    if (Initialized == false) {
        lv_obj_set_width(ui_Label_Main_Thermal_Pixel_Temperature, CROSSHAIR_CONTAINER_W);
        Initialized = true;
    }

    int32_t LabelH = lv_obj_get_height(ui_Label_Main_Thermal_Pixel_Temperature);

    if (LabelH <= 0) {
        LabelH = LBL_H_FALLBACK;
    }

    /* Vertical flip (above <-> below crosshair). */
    TopWhenAbove = CY - LABEL_Y_OFFSET - LabelH / 2;

    if (FlippedY == false) {
        if (TopWhenAbove < 0) {
            FlippedY = true;
        }
    } else {
        if (TopWhenAbove >= HYSTERESIS) {
            FlippedY = false;
        }
    }

    dy = FlippedY ? LABEL_Y_OFFSET : -LABEL_Y_OFFSET;

    /* Horizontal flip via text-align.
     * Flip threshold = HalfContainerW: when the container's left/right edge crosses the
     * image boundary the text would otherwise be invisible — anchoring it to the near
     * (visible) side of the label keeps all digits on screen.
     *
     * Near left edge  (CX < HalfContainerW):
     *   Container left goes outside the image.  Anchor text to the right of the label
     *   (= the portion still inside the image) with LV_TEXT_ALIGN_RIGHT.
     *
     * Near right edge (CX > ImageW - HalfContainerW):
     *   Container right goes outside the image.  Anchor text to the left with
     *   LV_TEXT_ALIGN_LEFT. */
    if (FlippedLeft == false) {
        if (CX < HalfContainerW) {
            FlippedLeft = true;
        }
    } else {
        if (CX >= HalfContainerW + HYSTERESIS) {
            FlippedLeft = false;
        }
    }

    if (FlippedRight == false) {
        if (CX > ImageW - HalfContainerW) {
            FlippedRight = true;
        }
    } else {
        if (CX <= ImageW - HalfContainerW - HYSTERESIS) {
            FlippedRight = false;
        }
    }

    if (FlippedLeft == true) {
        TextAlign = LV_TEXT_ALIGN_RIGHT;
    } else if (FlippedRight == true) {
        TextAlign = LV_TEXT_ALIGN_LEFT;
    } else {
        TextAlign = LV_TEXT_ALIGN_CENTER;
    }

    lv_obj_set_style_text_align(ui_Label_Main_Thermal_Pixel_Temperature, TextAlign, LV_PART_MAIN);
    lv_obj_set_x(ui_Label_Main_Thermal_Pixel_Temperature, 0);
    lv_obj_set_y(ui_Label_Main_Thermal_Pixel_Temperature, dy);
}

/** @brief              Scale an RGB565 source frame to a smaller RGB565 destination frame using
 *                      nearest-neighbour interpolation with a precomputed source-X LUT.
 *                      For the fixed 3/4 downscale (320×240 → 240×180) the quality is
 *                      visually equivalent to bilinear at roughly 12× lower CPU cost per pixel.
 *                      The function is only ever called from the GUI task and therefore not
 *                      thread-safe.
 *  @note               Source data is expected in RGB565 big-endian format (esp32-camera output).
 *                      Destination is written as RGB565 little-endian (LVGL canvas format).
 *                      Both horizontal and vertical axes are pre-flipped so that the final image
 *                      appears upright after the 1800-unit LVGL rotation on ui_Image_Main_Thermal.
 *  @param p_Src        Pointer to source RGB565 frame data (big-endian, high byte at even offsets).
 *  @param SrcWidth     Source frame width in pixels.
 *  @param SrcHeight    Source frame height in pixels.
 *  @param p_Dst        Pointer to destination RGB565 frame buffer.
 *  @param DstWidth     Destination frame width in pixels (max CONFIG_GUI_WIDTH).
 *  @param DstHeight    Destination frame height in pixels.
 */
static void UI_Scale_Camera(const uint8_t *p_Src, uint32_t SrcWidth, uint32_t SrcHeight,
                            uint8_t *p_Dst, uint32_t DstWidth, uint32_t DstHeight)
{
    /* Precompute source-X indices right-to-left (horizontal mirror).
     * Static storage prevents stack pressure inside the large Task_GUI stack frame. */
    static uint32_t XLutSrc[CONFIG_GUI_WIDTH];

    for (uint32_t x = 0; x < DstWidth; x++) {
        uint32_t SrcXFixed = static_cast<uint32_t>((uint64_t)(DstWidth - 1 - x) * ((SrcWidth - 1) << 16) / DstWidth);

        XLutSrc[x] = SrcXFixed >> 16;
    }

    for (uint32_t y = 0; y < DstHeight; y++) {
        /* Nearest-neighbour Y source index. */
        uint32_t SrcYFixed = static_cast<uint32_t>((uint64_t)y * ((SrcHeight - 1) << 16) / DstHeight);
        const uint8_t *SrcRow = p_Src + (SrcYFixed >> 16) * SrcWidth * 2;

        /* Destination row pointer reversed for 180° pre-rotation.
         * Within the row, start at the last pixel and decrement to avoid a multiply per pixel.
         */
        uint16_t *DstPixel = reinterpret_cast<uint16_t *>(p_Dst + (DstHeight - 1 - y) * DstWidth * 2) + (DstWidth - 1);

        for (uint32_t x = 0; x < DstWidth; x++, DstPixel--) {
            /* Nearest-neighbour source pixel.
             * Camera outputs RGB565 big-endian; LVGL expects little-endian -> single byte swap.
             * __builtin_bswap16 compiles to one Xtensa BYTESWAP instruction.
             */
            const uint16_t *SrcPixel = reinterpret_cast<const uint16_t *>(SrcRow + XLutSrc[x] * 2);

            *DstPixel = __builtin_bswap16(*SrcPixel);
        }
    }
}

/** @brief          LVGL keypad read callback.
 *                  Maps the debounced displayboard input state (joystick + buttons) to a
 *                  single LVGL key event. First matching entry in KEY_MAP wins.
 *                  BTN1-4 use custom key codes to avoid interception by the LVGL group
 *                  navigation engine, and have key-repeat suppressed (rising edge only).
 *  @param p_Indev  Input device handle
 *  @param p_Data   Input device data
 */
static void Keypad_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    DevicesManager_Input_State_t State;
    uint32_t Key = 0;
    uint32_t CurrentBtnKey;
    bool Pressed = false;
    const char *p_KeyName = "?";

    static const struct {
        uint32_t Key;
        const char *Name;
    } KEY_MAP[] = {
        { GUI_KEYPAD_BTN1, "BTN1"  },
        { GUI_KEYPAD_BTN2, "BTN2"  },
        { GUI_KEYPAD_BTN3, "BTN3"  },
        { GUI_KEYPAD_BTN4, "BTN4"  },
    };

    if ((_GUITaskState.AppContext == NULL) || (_GUITaskState.AppContext->InputMutex == NULL)) {
        p_Data->state = LV_INDEV_STATE_RELEASED;

        return;
    }

    xSemaphoreTake(_GUITaskState.AppContext->InputMutex, portMAX_DELAY);
    memcpy(&State, &_GUITaskState.AppContext->InputState, sizeof(DevicesManager_Input_State_t));
    xSemaphoreGive(_GUITaskState.AppContext->InputMutex);

    const bool InputArray[] = {
        State.Button1, State.Button2, State.Button3, State.Button4,
    };

    for (size_t i = 0; i < (sizeof(KEY_MAP) / sizeof(KEY_MAP[0])); i++) {
        if (InputArray[i]) {
            Key = KEY_MAP[i].Key;
            p_KeyName = KEY_MAP[i].Name;
            Pressed = true;

            break;
        }
    }

    /* BTN1-4 are action buttons, not navigation buttons so we must suppress key-repeat.
     * LVGL re-fires LV_EVENT_KEY on every indev tick while PRESSED is reported,
     * which would trigger multiple screen changes per button press.
     * Only the rising edge (first tick) passes through; subsequent ticks with
     * the same button still held are suppressed by reporting RELEASED.
     */
    CurrentBtnKey = ((Key >= GUI_KEYPAD_BTN1) && (Key <= GUI_KEYPAD_BTN4)) ? Key : 0;
    if ((CurrentBtnKey != 0) && (CurrentBtnKey == _GUITaskState.PrevBtnKey)) {
        Key = 0;
        Pressed = false;
    }

    _GUITaskState.PrevBtnKey = CurrentBtnKey;

    p_Data->key = Key;
    p_Data->state = Pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    /* Log only on edge transitions to avoid spamming every LVGL tick */
    static bool PrevPressed = false;
    static uint32_t PrevKey = 0;

    if ((Pressed != PrevPressed) || (Pressed && (Key != PrevKey))) {
        if (Pressed) {
            ESP_LOGD(TAG, "Keypad: %s pressed", p_KeyName);
        } else {
            ESP_LOGD(TAG, "Keypad: released");
        }
    }

    PrevPressed = Pressed;
    PrevKey = Key;

    if (lv_display_get_screen_active(lv_display_get_default()) == ui_Main) {
        TickType_t NowTick = xTaskGetTickCount();

        /* JoyCenter long-press: signalled by DevicesTask via JoyCenterLongPress in shared input state.
         * Rising edge toggles the crosshair in thermal view only; camera view is unaffected. */
        if ((State.JoyCenterLongPress == true) && (_GUITaskState.PrevJoyCenterLongPress == false)) {
            _GUITaskState.LongPressHandled = true;

            if (_GUITaskState.ShowCameraView == false) {
                _GUITaskState.ShowCrosshair = !_GUITaskState.ShowCrosshair;

                if (_GUITaskState.ShowCrosshair) {
                    ESP_LOGD(TAG, "Crosshair enabled");

                    lv_obj_remove_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
                    GUI_UpdateTempLabelPosition(_GUITaskState.CrosshairX, _GUITaskState.CrosshairY);
                } else {
                    ESP_LOGD(TAG, "Crosshair disabled");

                    lv_obj_add_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }

        /* JoyCenter: falling edge -> autofocus (only when no long-press was handled this hold). */
        if ((State.JoyCenter == false) && (_GUITaskState.PrevJoyCenter == true)) {
            if (_GUITaskState.LongPressHandled == false) {
                ESP_LOGD(TAG, "Autofocus triggered by joystick center");

                esp_event_post(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_REQUEST_FOCUS, NULL, 0, pdMS_TO_TICKS(100));
            }

            _GUITaskState.LongPressHandled = false;
        }

        /* Crosshair movement with joystick directions */
        if (_GUITaskState.ShowCrosshair) {
            bool AnyDirNow = State.JoyUp || State.JoyDown || State.JoyLeft || State.JoyRight;
            bool AnyDirPrev = _GUITaskState.PrevJoyUp || _GUITaskState.PrevJoyDown ||
                              _GUITaskState.PrevJoyLeft || _GUITaskState.PrevJoyRight;
            bool ShouldMove = false;

            if (AnyDirNow && (AnyDirPrev == false)) {
                /* Rising edge: start hold timer. The first move is deferred by
                 * CROSSHAIR_INITIAL_DELAY_MS to filter out joystick-bounce glitches
                 * that would cause a single physical press to register as two steps. */
                _GUITaskState.JoyDirHeldSince = NowTick;
                _GUITaskState.JoyDirLastMoveTick = 0;
            } else if (AnyDirNow && (_GUITaskState.JoyDirHeldSince != 0)) {
                TickType_t HeldFor = NowTick - _GUITaskState.JoyDirHeldSince;

                if (_GUITaskState.JoyDirLastMoveTick == 0) {
                    /* First move: fire once after the initial debounce delay. */
                    if (HeldFor >= pdMS_TO_TICKS(CROSSHAIR_INITIAL_DELAY_MS)) {
                        ShouldMove = true;
                        _GUITaskState.JoyDirLastMoveTick = NowTick;
                    }
                } else if ((HeldFor >= pdMS_TO_TICKS(CROSSHAIR_AUTOREPEAT_DELAY_MS)) &&
                           ((NowTick - _GUITaskState.JoyDirLastMoveTick) >= pdMS_TO_TICKS(CROSSHAIR_AUTOREPEAT_PERIOD_MS))) {
                    /* Auto-repeat: fires at CROSSHAIR_AUTOREPEAT_PERIOD_MS intervals
                     * after CROSSHAIR_AUTOREPEAT_DELAY_MS of continuous hold. */
                    ShouldMove = true;
                    _GUITaskState.JoyDirLastMoveTick = NowTick;
                }
            }

            if (AnyDirNow == false) {
                _GUITaskState.JoyDirHeldSince = 0;
                _GUITaskState.JoyDirLastMoveTick = 0;
            }

            if (ShouldMove) {
                /* Move the crosshair container.  CrosshairX/Y hold the centre pixel of the
                 * crosshair marker; the container is offset by half its size so the marker
                 * sits exactly at that pixel.  Read position from our own state rather than
                 * from lv_obj_get_x/y() to avoid stale coord values between layout passes. */
                int32_t Cx = _GUITaskState.CrosshairX;
                int32_t Cy = _GUITaskState.CrosshairY;
                int32_t MaxX = static_cast<int32_t>(UI_IMAGE_CANVAS_WIDTH)  - 1;
                int32_t MaxY = static_cast<int32_t>(UI_IMAGE_CANVAS_HEIGHT) - 1;

                if (State.JoyUp)    {
                    Cy -= static_cast<int32_t>(CROSSHAIR_STEP_PX);
                }

                if (State.JoyDown)  {
                    Cy += static_cast<int32_t>(CROSSHAIR_STEP_PX);
                }

                if (State.JoyLeft)  {
                    Cx -= static_cast<int32_t>(CROSSHAIR_STEP_PX);
                }

                if (State.JoyRight) {
                    Cx += static_cast<int32_t>(CROSSHAIR_STEP_PX);
                }

                if (Cx < 0)    {
                    Cx = 0;
                }

                if (Cx > MaxX) {
                    Cx = MaxX;
                }

                if (Cy < 0)    {
                    Cy = 0;
                }

                if (Cy > MaxY) {
                    Cy = MaxY;
                }

                _GUITaskState.CrosshairX = Cx;
                _GUITaskState.CrosshairY = Cy;
                lv_obj_set_pos(ui_Container_Main_Thermal_Crosshair,
                               Cx - CROSSHAIR_CONTAINER_W / 2,
                               Cy - CROSSHAIR_CONTAINER_H / 2);
                GUI_UpdateTempLabelPosition(Cx, Cy);

                ESP_LOGD(TAG, "Crosshair moved to (%d, %d)", Cx, Cy);
            }
        }
    }

    _GUITaskState.PrevJoyCenter = State.JoyCenter;
    _GUITaskState.PrevJoyCenterLongPress = State.JoyCenterLongPress;
    _GUITaskState.PrevJoyUp = State.JoyUp;
    _GUITaskState.PrevJoyDown = State.JoyDown;
    _GUITaskState.PrevJoyLeft = State.JoyLeft;
    _GUITaskState.PrevJoyRight = State.JoyRight;
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
     * NOTE: x_lut_xi is NOT stored - computing 256 - XFrac inline avoids a uint8_t
     * overflow: when XFrac == 0, 256 would truncate to 0, zeroing all weights leads to black
     * pixels. XInv is computed as uint32_t in the inner loop instead.
     */
    uint8_t XLutX0[CONFIG_GUI_WIDTH];
    uint8_t XLutX1[CONFIG_GUI_WIDTH];
    uint8_t XLutXf[CONFIG_GUI_WIDTH];

    esp_task_wdt_add(NULL);

    App_Context = static_cast<App_Context_t *>(p_Parameters);
    _GUITaskState.AppContext = App_Context;
    ESP_LOGD(TAG, "GUI Task started on core %d", xPortGetCoreID());

    Timeout = 0;
    lv_label_set_text(ui_SplashScreen_StatusText, "Booting...");
    do {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_GUITaskState.EventGroup);
        if (EventBits & GUI_TASK_CAMERA_READY) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Camera ready");
            lv_bar_set_value(ui_SplashScreen_LoadingBar, lv_bar_get_value(ui_SplashScreen_LoadingBar) + 50, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_READY);
        } else if (EventBits & GUI_TASK_LEPTON_READY) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Lepton ready");
            lv_bar_set_value(ui_SplashScreen_LoadingBar, lv_bar_get_value(ui_SplashScreen_LoadingBar) + 50, LV_ANIM_ON);

            GUI_Helper_InitTouch(&_GUITaskState, Touch_LVGL_ReadCallback);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_READY);
        } else if (EventBits & GUI_TASK_LEPTON_ERROR) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Lepton Error");
            lv_bar_set_value(ui_SplashScreen_LoadingBar, 99, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_ERROR);
        } else if (EventBits & GUI_TASK_CAMERA_ERROR) {
            lv_label_set_text(ui_SplashScreen_StatusText, "Camera Error");
            lv_bar_set_value(ui_SplashScreen_LoadingBar, 99, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_ERROR);
        } else if (Timeout >= 30000) {
            break;
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
        Timeout += 100;
    } while (lv_bar_get_value(ui_SplashScreen_LoadingBar) < lv_bar_get_max_value(ui_SplashScreen_LoadingBar));

    lv_disp_load_scr(ui_Main);

    /* Process layout changes after loading new screen. */
    lv_timer_handler();

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, NULL, 0, pdMS_TO_TICKS(500));

    /* Set the initial ROI first to give it a size. */
    SettingsManager_GetLepton(&LeptonSettings);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SPOTMETER]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_SCENE]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_AGC]);
    GUI_Update_ROI(LeptonSettings.ROI[ROI_TYPE_VIDEO_FOCUS]);

    GUI_Update_Info();

    /* Show the crosshair at boot, centred on the canvas.
     * CrosshairX/Y store the centre pixel of the crosshair marker (not the container's
     * top-left corner).  lv_obj_set_pos() is called with (CX - W/2, CY - H/2) so that
     * the marker sits exactly at the stored position; negative values are valid and allow
     * the crosshair to reach every edge of the image.
     * LV_ALIGN_TOP_LEFT is required so that LVGL treats the position as an absolute offset
     * from the parent's top-left origin; without it negative offsets are clamped to 0.
     */
    _GUITaskState.ShowCrosshair = true;
    _GUITaskState.CrosshairX = static_cast<int32_t>(UI_IMAGE_CANVAS_WIDTH)  / 2;
    _GUITaskState.CrosshairY = static_cast<int32_t>(UI_IMAGE_CANVAS_HEIGHT) / 2;
    lv_obj_set_align(ui_Container_Main_Thermal_Crosshair, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_Container_Main_Thermal_Crosshair,
                   _GUITaskState.CrosshairX - CROSSHAIR_CONTAINER_W / 2,
                   _GUITaskState.CrosshairY - CROSSHAIR_CONTAINER_H / 2);

    lv_obj_remove_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
    GUI_UpdateTempLabelPosition(_GUITaskState.CrosshairX, _GUITaskState.CrosshairY);

    /* Initialize SD card icon based on current storage state. */
    if (MemoryManager_HasSDCard()) {
        lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);
    }

    /* Variables for Illuminance values for the scene label.
     * Fetch all these values at the beginning to not waste CPU performance because these
     * functions are not simple get functions
     *      0 = Max label
     *      1 = Min label
     *      2 = Mean label
     *      3 = Crosshair label
     *      4 = Pixel temperature label
     *
     * NOTE on coordinate spaces:
     * - Labels [3] (Crosshair) and [4] (PixelTemperature) are DIRECT children of
     *   ui_Image_Main_Thermal, so lv_obj_get_x/y() already returns image-relative coords.
     * - Labels [0..2] (Max/Min/Mean) are children of ui_Container_Main_Thermal_Scene_Statistics,
     *   which itself is a child of ui_Image_Main_Thermal. Their lv_obj_get_x/y() is relative
     *   to the container, so the container's own offset within the image must be added.
     */
    int32_t SceneStatsContainerX = lv_obj_get_x(ui_Container_Main_Thermal_Scene_Statistics);
    int32_t SceneStatsContainerY = lv_obj_get_y(ui_Container_Main_Thermal_Scene_Statistics);

    /* Indices [0..2]: fixed positions relative to ui_Image_Main_Thermal (Scene Statistics labels).
     * Indices [3..4]: crosshair / pixel-temperature labels – their parent container moves, so the
     *                 image-relative position is updated every frame inside the while-loop below. */
    int32_t SceneLabelX0[5] = { SceneStatsContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Max),
                                SceneStatsContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Min),
                                SceneStatsContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Scene_Mean),
                                0,  /* Updated per-frame: container_x + label_x */
                                0   /* Updated per-frame: container_x + label_x */
                              };
    int32_t SceneLabelY0[5] = { SceneStatsContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Max),
                                SceneStatsContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Min),
                                SceneStatsContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Scene_Mean),
                                0,  /* Updated per-frame: container_y + label_y */
                                0   /* Updated per-frame: container_y + label_y */
                              };
    int32_t SceneLabelW[5] = { lv_obj_get_width(ui_Label_Main_Thermal_Scene_Max),
                               lv_obj_get_width(ui_Label_Main_Thermal_Scene_Min),
                               lv_obj_get_width(ui_Label_Main_Thermal_Scene_Mean),
                               lv_obj_get_width(ui_Label_Main_Thermal_Crosshair),
                               lv_obj_get_width(ui_Label_Main_Thermal_Pixel_Temperature)
                             };
    int32_t SceneLabelH[5] = { lv_obj_get_height(ui_Label_Main_Thermal_Scene_Max),
                               lv_obj_get_height(ui_Label_Main_Thermal_Scene_Min),
                               lv_obj_get_height(ui_Label_Main_Thermal_Scene_Mean),
                               lv_obj_get_height(ui_Label_Main_Thermal_Crosshair),
                               lv_obj_get_height(ui_Label_Main_Thermal_Pixel_Temperature)
                             };

    while (_GUITaskState.IsRunning) {
        EventBits_t EventBits;
        App_Lepton_Frame_t LeptonFrame;

        esp_task_wdt_reset();

        /* Check for new thermal frame. */
        if (xQueueReceive(App_Context->Lepton_FrameQueue, &LeptonFrame, 0) == pdTRUE) {
            if (_GUITaskState.IsUVCStreaming || _GUITaskState.ShowCameraView) {
                ESP_LOGD(TAG, "UVC streaming or camera view active - skipping GUI thermal frame processing");
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

                /* Scale from source to destination using bilinear interpolation. */
                Dst = _GUITaskState.ThermalCanvasBuffer;
                ImageWidth = lv_obj_get_width(ui_Image_Main_Image);
                ImageHeight = lv_obj_get_height(ui_Image_Main_Image);

                /* Skip if image widget not properly initialized yet. */
                if ((ImageWidth == 0) || (ImageHeight == 0)) {
                    ESP_LOGW(TAG, "Image widget not ready yet (size: %ux%u), skipping frame", ImageWidth, ImageHeight);

                    continue;
                }

                /* Update crosshair overlay label positions every frame.
                 * ui_Label_Main_Thermal_Crosshair and ui_Label_Main_Thermal_PixelTemperature are
                 * children of ui_Container_Main_Thermal_Crosshair (100×50), not direct children
                 * of ui_Image_Main_Thermal. The container position changes with every joystick move,
                 * so the image-relative coordinates must be recomputed here. */
                {
                    int32_t ContainerX = lv_obj_get_x(ui_Container_Main_Thermal_Crosshair);
                    int32_t ContainerY = lv_obj_get_y(ui_Container_Main_Thermal_Crosshair);

                    SceneLabelX0[3] = ContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Crosshair);
                    SceneLabelY0[3] = ContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Crosshair);
                    SceneLabelX0[4] = ContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Pixel_Temperature);
                    SceneLabelY0[4] = ContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Pixel_Temperature);
                }

                for (uint32_t x = 0; x < ImageWidth; x++) {
                    uint32_t SrcXFixed = x * ((LeptonFrame.Width - 1) << 16) / ImageWidth;
                    uint32_t Xp = SrcXFixed >> 16;
                    XLutX0[x] = static_cast<uint8_t>(Xp);
                    XLutX1[x] = static_cast<uint8_t>(((Xp + 1) < LeptonFrame.Width) ? (Xp + 1) : Xp);
                    XLutXf[x] = static_cast<uint8_t>((SrcXFixed >> 8) & 0xFF);
                }

                for (uint32_t y = 0; y < ImageHeight; y++) {
                    /* Reset watchdog every 20 rows to prevent timeout during image processing. */
                    if ((y % 20) == 0) {
                        esp_task_wdt_reset();
                    }

                    uint32_t src_y_fixed = y * ((LeptonFrame.Height - 1) << 16) / ImageHeight;
                    uint32_t Y0 = src_y_fixed >> 16;
                    uint32_t Y1 = ((Y0 + 1) < LeptonFrame.Height) ? (Y0 + 1) : Y0;
                    uint32_t YFrac = (src_y_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                    uint32_t YInv = 256 - YFrac;

                    for (uint32_t x = 0; x < ImageWidth; x++) {
                        /* Use precomputed x LUT - avoids 1 multiply + 1 divide per pixel per row.
                         * XInv is computed inline (not cached) to avoid uint8_t overflow when XFrac==0. */
                        uint32_t X0 = XLutX0[x];
                        uint32_t X1 = XLutX1[x];
                        uint32_t XFrac = XLutXf[x];
                        uint32_t XInv = 256u - XFrac;

                        /* Get the four surrounding pixels. */
                        uint32_t Idx00 = ((Y0 * LeptonFrame.Width) + X0) * 3;
                        uint32_t Idx10 = ((Y0 * LeptonFrame.Width) + X1) * 3;
                        uint32_t Idx01 = ((Y1 * LeptonFrame.Width) + X0) * 3;
                        uint32_t Idx11 = ((Y1 * LeptonFrame.Width) + X1) * 3;

                        /* Bilinear interpolation using fixed-point arithmetic (8.8 format) */
                        /* Weight: (256 - XFrac) * (256 - YFrac), XFrac*(256 - YFrac), etc. */
                        uint32_t W00 = (XInv * YInv) >> 8;
                        uint32_t W10 = (XFrac * YInv) >> 8;
                        uint32_t W01 = (XInv * YFrac) >> 8;
                        uint32_t W11 = (XFrac * YFrac) >> 8;

                        uint32_t R = (LeptonFrame.Buffer[Idx00 + 0] * W00 +
                                      LeptonFrame.Buffer[Idx10 + 0] * W10 +
                                      LeptonFrame.Buffer[Idx01 + 0] * W01 +
                                      LeptonFrame.Buffer[Idx11 + 0] * W11) >> 8;

                        uint32_t G = (LeptonFrame.Buffer[Idx00 + 1] * W00 +
                                      LeptonFrame.Buffer[Idx10 + 1] * W10 +
                                      LeptonFrame.Buffer[Idx01 + 1] * W01 +
                                      LeptonFrame.Buffer[Idx11 + 1] * W11) >> 8;

                        uint32_t B = (LeptonFrame.Buffer[Idx00 + 2] * W00 +
                                      LeptonFrame.Buffer[Idx10 + 2] * W10 +
                                      LeptonFrame.Buffer[Idx01 + 2] * W01 +
                                      LeptonFrame.Buffer[Idx11 + 2] * W11) >> 8;

                        /* Inside the image area under the label: Add the Luminance
                         * Note: The image widget has 180° rotation applied via lv_image_set_rotation.
                         * - XRot = Image_Width - 1 - x (X axis is inverted due to rotation)
                         * - y is used directly (Y axis matches label position directly)
                         */
                        /* Map buffer coordinates to display coordinates for 180� rotated image:
                         * x: XRot = ImageWidth - 1 - x  (horizontal mirror)
                         * y: YRot = ImageHeight - 1 - y  (vertical mirror)
                         * Labels use display coordinates, so both axes must be inverted.
                         */
                        uint32_t XRot = ImageWidth - 1 - x;
                        uint32_t YRot = ImageHeight - 1 - y;

                        /* Accumulate BT.601 luma (77*R + 150*G + 29*B) >> 8 under each overlay label.
                         * Coefficients sum to 256 so the final shift is lossless for 8-bit values. */
                        for (size_t Lbl = 0; Lbl < (sizeof(SceneLabelX0) / sizeof(SceneLabelX0[0])); Lbl++) {
                            if ((XRot >= static_cast<uint32_t>(SceneLabelX0[Lbl])) &&
                                (XRot < static_cast<uint32_t>(SceneLabelX0[Lbl] + SceneLabelW[Lbl])) &&
                                (YRot >= static_cast<uint32_t>(SceneLabelY0[Lbl])) &&
                                (YRot < static_cast<uint32_t>(SceneLabelY0[Lbl] + SceneLabelH[Lbl]))) {
                                SceneLabelIlluminance[Lbl] += ((77u * R) + (150u * G) + (29u * B)) >> 8u;
                                SceneLabelCount[Lbl]++;
                            }
                        }

                        uint32_t DstIdx = (y * ImageWidth) + x;

                        /* Convert to RGB565 - LVGL handles swapping with RGB565_SWAPPED */
                        uint16_t Rgb565 = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);

                        /* Low byte first */
                        Dst[(DstIdx * 2) + 0] = Rgb565 & 0xFF;

                        /* High byte second */
                        Dst[(DstIdx * 2) + 1] = (Rgb565 >> 8) & 0xFF;
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

                /* Update text colour of each overlay label based on background luminance. */
                lv_obj_t *const SceneLabels[] = {
                    ui_Label_Main_Thermal_Scene_Max,
                    ui_Label_Main_Thermal_Scene_Min,
                    ui_Label_Main_Thermal_Scene_Mean,
                    ui_Label_Main_Thermal_Crosshair,
                    ui_Label_Main_Thermal_Pixel_Temperature,
                };

                for (size_t i = 0; i < (sizeof(SceneLabels) / sizeof(SceneLabels[0])); i++) {
                    lv_obj_set_style_text_color(SceneLabels[i],
                                                (SceneLabelAverageLuminance[i] > 128) ? lv_color_black() : lv_color_white(),
                                                LV_PART_MAIN);
                }

                /* Reset watchdog after image processing */
                esp_task_wdt_reset();

                /* Max temperature (top of gradient) */
                float TempMaxCelsius = (LeptonFrame.Max / 100.0f) - 273.15f;
                snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", TempMaxCelsius);
                lv_label_set_text(ui_Label_Main_TempScaleMax, Buffer);

                /* Min temperature (bottom of gradient) */
                float TempMinCelsius = (LeptonFrame.Min / 100.0f) - 273.15f;
                snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", TempMinCelsius);
                lv_label_set_text(ui_Label_Main_TempScaleMin, Buffer);

                /* Trigger LVGL to redraw the image */
                lv_obj_invalidate(ui_Image_Main_Image);
                ESP_LOGD(TAG, "Updated thermal image display (src: %ux%u -> dst: %ux%u)", LeptonFrame.Width, LeptonFrame.Height,
                         ImageWidth, ImageHeight);

                /* Update network frame for server streaming if server is running */
                if (Server_IsRunning()) {
                    if (xSemaphoreTake(_GUITaskState.NetworkFrame.Mutex, 0) == pdTRUE) {
                        /* Convert scaled RGB565 buffer to RGB888 for network transmission */
                        uint8_t *Rgb888Dst = _GUITaskState.NetworkRGBBuffer;

                        /* Reset watchdog before RGB conversion */
                        esp_task_wdt_reset();

                        for (uint32_t i = 0; i < (ImageWidth * ImageHeight); i++) {
                            /* Read RGB565 value (little endian) */
                            uint16_t Rgb565 = Dst[(i * 2) + 0] | (Dst[(i * 2) + 1] << 8);

                            /* Convert RGB565 to RGB888 */
                            uint8_t R = (Rgb565 >> 8) & 0xF8;
                            uint8_t G = (Rgb565 >> 3) & 0xFC;
                            uint8_t B = (Rgb565 << 3) & 0xF8;

                            /* Store as RGB888 */
                            Rgb888Dst[(i * 3) + 0] = R;
                            Rgb888Dst[(i * 3) + 1] = G;
                            Rgb888Dst[(i * 3) + 2] = B;
                        }

                        _GUITaskState.NetworkFrame.Buffer = _GUITaskState.NetworkRGBBuffer;
                        _GUITaskState.NetworkFrame.Width = ImageWidth;
                        _GUITaskState.NetworkFrame.Height = ImageHeight;
                        _GUITaskState.NetworkFrame.Timestamp = esp_timer_get_time() / 1000;

                        xSemaphoreGive(_GUITaskState.NetworkFrame.Mutex);

                        /* Reset watchdog after RGB conversion */
                        esp_task_wdt_reset();
                    }

                    Server_NotifyClients();
                }
            }
        }

        /* Check for new visible-light camera frame. */
        if (App_Context->Camera_FrameQueue != NULL) {
            App_Camera_Frame_t CameraFrame;

            if (xQueueReceive(App_Context->Camera_FrameQueue, &CameraFrame, 0) == pdTRUE) {
                if (_GUITaskState.ShowCameraView) {
                    /* Scale camera frame down and render in the thermal canvas */
                    UI_Scale_Camera(CameraFrame.Buffer, CameraFrame.Width, CameraFrame.Height,
                                    _GUITaskState.ThermalCanvasBuffer, UI_IMAGE_CANVAS_WIDTH, UI_IMAGE_CANVAS_HEIGHT);
                    lv_obj_invalidate(ui_Image_Main_Image);
                }
            }
        }

        /* Save the currently displayed frame (thermal or camera) if requested.
         * A dedicated snapshot copy (SaveCanvasBuffer) is used to prevent a data race
         * between Task_GUI overwriting ThermalCanvasBuffer and Task_ImageSave reading it.
         */
        if (_GUITaskState.SaveNextFrameRequested) {
            App_Lepton_Frame_t SaveFrame;

            _GUITaskState.SaveNextFrameRequested = false;

            /* Take a snapshot of the current canvas before handing the pointer to Task_ImageSave. */
            memcpy(_GUITaskState.SaveCanvasBuffer, _GUITaskState.ThermalCanvasBuffer,
                   UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2);

            SaveFrame.Buffer = _GUITaskState.SaveCanvasBuffer;
            SaveFrame.Width = UI_IMAGE_CANVAS_WIDTH;
            SaveFrame.Height = UI_IMAGE_CANVAS_HEIGHT;

            /* RGB565 = 2 bytes per pixel */
            SaveFrame.Channels = 2;
            SaveFrame.Min = 0;
            SaveFrame.Max = 0;

            if (xQueueSend(_GUITaskState.ImageSaveQueue, &SaveFrame, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Image save queue full, skipping save");

                esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, NULL, 0, pdMS_TO_TICKS(100));
            }
        }

        EventBits = xEventGroupGetBits(_GUITaskState.EventGroup);
        if (EventBits & GUI_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _GUITaskState.IsRunning = false;

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_STOP_REQUEST);

            break;
        }

        if (EventBits & GUI_TASK_BATTERY_STATUS_CHANGED) {
            char Buffer[16];

            /* Select battery icon and background tint based on charge level. */
            static const struct {
                uint8_t Threshold;
                const char *Icon;
                uint32_t Color;
            } BATTERY_TABLE[] = {
                { 100, LV_SYMBOL_BATTERY_FULL,  0x00FF00U },
                {  75, LV_SYMBOL_BATTERY_3,     0x00FF00U },
                {  50, LV_SYMBOL_BATTERY_2,     0xFFFF00U },
                {  25, LV_SYMBOL_BATTERY_1,     0xFFFF00U },
                {   0, LV_SYMBOL_BATTERY_EMPTY, 0xFF0000U },
            };

            const char *Icon = BATTERY_TABLE[4].Icon;
            uint32_t Color = BATTERY_TABLE[4].Color;

            for (size_t i = 0; i < (sizeof(BATTERY_TABLE) / sizeof(BATTERY_TABLE[0])); i++) {
                if (_GUITaskState.BatteryInfo.Percentage >= BATTERY_TABLE[i].Threshold) {
                    Icon = BATTERY_TABLE[i].Icon;
                    Color = BATTERY_TABLE[i].Color;

                    break;
                }
            }

            snprintf(Buffer, sizeof(Buffer), "%d%%", _GUITaskState.BatteryInfo.Percentage);
            lv_label_set_text(ui_Label_Main_Battery_Remaining, Buffer);

            lv_label_set_text(ui_Label_Main_Battery_Remaining_Icon, Icon);
            lv_obj_set_style_bg_color(ui_Label_Main_Battery_Remaining_Icon, lv_color_hex(Color), 0);

            if (_GUITaskState.BatteryInfo.Charging) {
                lv_label_set_text(ui_Label_Info_Battery_Status_Value, "Charging");
                lv_obj_set_style_text_color(ui_Label_Main_Battery_Remaining_Icon, lv_color_hex(0x00FF00), LV_PART_MAIN);
            } else {
                lv_label_set_text(ui_Label_Info_Battery_Status_Value, "Not charging");
                lv_obj_set_style_text_color(ui_Label_Main_Battery_Remaining_Icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
            }

            lv_label_set_text(ui_Label_Info_Battery_Remaining_Value, Buffer);
            lv_bar_set_value(ui_Bar_Info_Battery_Remaining, _GUITaskState.BatteryInfo.Percentage, LV_ANIM_OFF);

            snprintf(Buffer, sizeof(Buffer), "%d mV", _GUITaskState.BatteryInfo.Voltage);
            lv_label_set_text(ui_Label_Info_Battery_Voltage_Value, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_BATTERY_STATUS_CHANGED);
        }

        if (EventBits & GUI_TASK_WIFI_CONNECTION_STATE_CHANGED) {
            if (_GUITaskState.WiFiConnected) {
                char Buffer[32];

                snprintf(Buffer, sizeof(Buffer), "IP: %lu.%lu.%lu.%lu",
                         (_GUITaskState.IP_Info.IP >> 0) & 0xFF,
                         (_GUITaskState.IP_Info.IP >> 8) & 0xFF,
                         (_GUITaskState.IP_Info.IP >> 16) & 0xFF,
                         (_GUITaskState.IP_Info.IP >> 24) & 0xFF);

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

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_PROVISIONING_STATE_CHANGED) {
            if ((_GUITaskState.WiFiConnected == false) && _GUITaskState.ProvisioningActive) {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF8800), LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_SD_CARD_STATE_CHANGED) {
            if (_GUITaskState.CardPresent) {
                if (MemoryManager_HasSDCard()) {
                    /* MemoryManager already auto-mounted the SD card at init - just confirm green */
                    lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);

                    ESP_LOGD(TAG, "SD card already mounted by MemoryManager");
                } else {
                    /* Card present but not yet mounted: show orange while mounting */
                    lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0xFF8800), LV_PART_MAIN);

                    esp_task_wdt_reset();
                    if (MemoryManager_SwitchToSDCard() == ESP_OK) {
                        /* Successfully mounted: show green */
                        lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0x00FF00), LV_PART_MAIN);

                        ESP_LOGD(TAG, "Storage switched to SD card");
                    } else {
                        /* Mount failed: stay orange (card present but not usable) */
                        ESP_LOGW(TAG, "SD card detected but mount failed - keeping orange");
                    }

                    esp_task_wdt_reset();
                }
            } else {
                /* Card removed: switch back to internal only if we are currently on SD */
                if (MemoryManager_HasSDCard()) {
                    esp_task_wdt_reset();
                    MemoryManager_SwitchToInternal();
                    esp_task_wdt_reset();

                    ESP_LOGD(TAG, "Storage switched back to internal flash");
                }

                lv_obj_set_style_text_color(ui_Image_Main_SDCard, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_SD_CARD_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_LEPTON_UPTIME_READY) {
            char Buffer[32];
            uint32_t Uptime;

            Uptime = _GUITaskState.LeptonUptime / 1000;

            snprintf(Buffer, sizeof(Buffer), "%02lu:%02lu:%02lu", Uptime / 3600, (Uptime % 3600) / 60, Uptime % 60);
            lv_label_set_text(ui_Label_Info_Lepton_Uptime, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_UPTIME_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_TEMPERATURE_READY) {
            char Buffer[32];

            snprintf(Buffer, sizeof(Buffer), "%.2f \xC2\xB0""C", _GUITaskState.LeptonTemperatures.FPA);
            lv_label_set_text(ui_Label_Info_Lepton_FPA, Buffer);
            snprintf(Buffer, sizeof(Buffer), "%.2f \xC2\xB0""C", _GUITaskState.LeptonTemperatures.AUX);
            lv_label_set_text(ui_Label_Info_Lepton_AUX, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_TEMPERATURE_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY) {
            char Buffer[16];

            snprintf(Buffer, sizeof(Buffer), "%.2f \xC2\xB0""C", _GUITaskState.SpotTemperature);
            lv_label_set_text(ui_Label_Main_Thermal_Pixel_Temperature, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY);
        }

        if (EventBits & GUI_TASK_LEPTON_SCENE_STATISTICS_READY) {
            char Buffer[16];
            float Temp;

            Temp = _GUITaskState.ROIResult.Max;
            snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Max, Buffer);

            Temp = _GUITaskState.ROIResult.Min;
            snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Min, Buffer);

            Temp = _GUITaskState.ROIResult.Mean ;
            snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", Temp);
            lv_label_set_text(ui_Label_Main_Thermal_Scene_Mean, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_SCENE_STATISTICS_READY);
        }

        if (EventBits & GUI_TASK_UVC_STREAMING_STATE_CHANGED) {
            if (_GUITaskState.IsUVCStreaming) {
                /* Clear thermal canvas to black */
                memset(_GUITaskState.ThermalCanvasBuffer, 0x00, UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2);
                lv_obj_invalidate(ui_Image_Main_Image);

                /* Show UVC overlay */
                lv_obj_remove_flag(_GUITaskState.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);

                /* Hide ROI rectangles */
                lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);

                /* Hide temperature labels */
                lv_obj_add_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);

                /* Hide temperature scale labels */
                lv_obj_add_flag(ui_Label_Main_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
            } else {
                /* Hide UVC overlay */
                lv_obj_add_flag(_GUITaskState.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);

                /* Restore ROI rectangles */
                if (_GUITaskState.ShowROI) {
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                }

                /* Restore temperature labels */
                lv_obj_remove_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);

                /* Restore temperature scale labels */
                lv_obj_remove_flag(ui_Label_Main_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_CAMERA_VIEW_CHANGED) {
            /* Leaving thermal view: restore camera overlay */
            if (_GUITaskState.ShowCameraView) {
                bool FlashEnabled;

                memset(_GUITaskState.ThermalCanvasBuffer, 0x00, UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2);

                /* Hide temperature overlay and scale labels */
                lv_obj_add_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Gradient, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text(ui_Label_Main_Button_ROI, "\uF0EB");

                DevicesManager_IsFlashEnabled(&FlashEnabled);
                if (FlashEnabled) {
                } else {
                }
            }
            /* Leaving camera view: restore temperature overlay */
            else {
                DevicesManager_SetFlashEnable(false);

                lv_obj_remove_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_TempScaleMax, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_TempScaleMin, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Gradient, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text(ui_Label_Main_Button_ROI, "\uE595");

                if (_GUITaskState.ShowCrosshair) {
                    lv_obj_remove_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
                }

                if (_GUITaskState.ShowROI) {
                    lv_obj_remove_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                }
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_VIEW_CHANGED);
        }

        if (EventBits & GUI_TASK_GRADIENT_REDRAW_REQUIRED) {
            GUI_Task_UpdateGradient();

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_GRADIENT_REDRAW_REQUIRED);
        }

        if (EventBits & GUI_TASK_THERMAL_ROI_CHANGED) {
            /* RGB camera view */
            if (_GUITaskState.ShowCameraView) {
                DevicesManager_ToggleFlashEnable();
            }
            /* Thermal view */
            else {
                if (_GUITaskState.ShowROI) {
                    lv_obj_remove_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);
                }
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_THERMAL_ROI_CHANGED);
        }

        if (EventBits & GUI_TASK_TEMPERATURE_SENSOR_READY) {
            char Buffer[16];
            float Offset;
            Settings_Calibration_t Calibration;

            SettingsManager_GetCalibration(&Calibration);

            Offset = static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration;

            snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", _GUITaskState.TemperatureInfo.TempSensor + Offset);
            lv_label_set_text(ui_Label_Main_Statusbar_Temperatur, Buffer);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_TEMPERATURE_SENSOR_READY);
        }

        if (EventBits & GUI_TASK_IMAGE_SAVE_COMPLETED) {
            MessageBox_CloseProgress();
            MessageBox_ImageSaveError(ESP_OK);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_IMAGE_SAVE_COMPLETED);
        }

        if (EventBits & GUI_TASK_IMAGE_SAVE_FAILED_BIT) {
            MessageBox_CloseProgress();
            MessageBox_ImageSaveError(ESP_FAIL);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_IMAGE_SAVE_FAILED_BIT);
        }

        if (EventBits & GUI_TASK_SCREEN_REFRESH_REQUIRED) {
            /* Re-render the LVGL overlay layer after PNG save. During the write,
             * LCD flushes are skipped to avoid SPI bus contention. After the save
             * completes, invalidate lv_layer_top() so that any LVGL changes that
             * happened during the write (e.g. message box closing) are properly
             * flushed to the display on the next lv_timer_handler() call.
             */
            lv_obj_invalidate(lv_layer_top());

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_SCREEN_REFRESH_REQUIRED);
        }

        _lock_acquire(&_GUITaskState.LVGL_API_Lock);
        lv_timer_handler();
        _lock_release(&_GUITaskState.LVGL_API_Lock);

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    _GUITaskState.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t GUI_Task_Init(void)
{
    BaseType_t Error;

    if (_GUITaskState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_ERROR_CHECK(GUI_Helper_Init(&_GUITaskState, Touch_LVGL_ReadCallback, GUI_LCD_Flush_CB));
    ESP_ERROR_CHECK(GUI_Helper_InitKeypad(&_GUITaskState, Keypad_LVGL_ReadCallback));

    ui_init();

    _GUITaskState.ThermalCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(UI_IMAGE_CANVAS_WIDTH *
                                                                                UI_IMAGE_CANVAS_HEIGHT * 2, MALLOC_CAP_SPIRAM));
    _GUITaskState.GradientCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(UI_GRADIENT_CANVAS_WIDTH *
                                                                                 UI_IMAGE_CANVAS_HEIGHT * 2, MALLOC_CAP_SPIRAM));
    _GUITaskState.NetworkRGBBuffer = static_cast<uint8_t *>(heap_caps_malloc(UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT
                                                                             * 3, MALLOC_CAP_SPIRAM));
    _GUITaskState.SaveCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT
                                                                             * 2, MALLOC_CAP_SPIRAM));

    if (_GUITaskState.ThermalCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate thermal canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.GradientCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate gradient canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.NetworkRGBBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network RGB buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.SaveCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate save canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);

        return ESP_ERR_NO_MEM;
    }

    _GUITaskState.ImageSaveQueue = xQueueCreate(1, sizeof(App_Lepton_Frame_t));
    if (_GUITaskState.ImageSaveQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create image save queue!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    Error = xTaskCreatePinnedToCore(Task_ImageSave, "Task_ImgSave", 8192, NULL, CONFIG_GUI_TASK_PRIO - 1,
                                    &_GUITaskState.ImageSaveTaskHandle, 1);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create image save task!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_FAIL);

        vQueueDelete(_GUITaskState.ImageSaveQueue);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        _GUITaskState.ImageSaveQueue = NULL;
        _GUITaskState.ThermalCanvasBuffer = NULL;
        _GUITaskState.GradientCanvasBuffer = NULL;
        _GUITaskState.NetworkRGBBuffer = NULL;
        _GUITaskState.SaveCanvasBuffer = NULL;

        return ESP_FAIL;
    }

    /* Initialize buffers with black pixels (RGB565 = 0x0000) */
    memset(_GUITaskState.ThermalCanvasBuffer, 0x00, UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2);
    memset(_GUITaskState.GradientCanvasBuffer, 0x00, UI_GRADIENT_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2);

    /* Now configure the image descriptors with allocated buffers */
    _GUITaskState.ThermalImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITaskState.ThermalImageDescriptor.header.w = UI_IMAGE_CANVAS_WIDTH;
    _GUITaskState.ThermalImageDescriptor.header.h = UI_IMAGE_CANVAS_HEIGHT;
    _GUITaskState.ThermalImageDescriptor.data = _GUITaskState.ThermalCanvasBuffer;
    _GUITaskState.ThermalImageDescriptor.data_size = UI_IMAGE_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2;

    _GUITaskState.GradientImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITaskState.GradientImageDescriptor.header.w = UI_GRADIENT_CANVAS_WIDTH;
    _GUITaskState.GradientImageDescriptor.header.h = UI_IMAGE_CANVAS_HEIGHT;
    _GUITaskState.GradientImageDescriptor.data = _GUITaskState.GradientCanvasBuffer;
    _GUITaskState.GradientImageDescriptor.data_size = UI_GRADIENT_CANVAS_WIDTH * UI_IMAGE_CANVAS_HEIGHT * 2;

    GUI_Task_UpdateGradient();

    lv_img_set_src(ui_Image_Main_Image, &_GUITaskState.ThermalImageDescriptor);
    lv_img_set_src(ui_Image_Main_Gradient, &_GUITaskState.GradientImageDescriptor);

    /* Create UVC streaming overlay label (hidden by default) */
    _GUITaskState.UVCOverlayLabel = lv_label_create(ui_Container_Main_Thermal);
    lv_label_set_text(_GUITaskState.UVCOverlayLabel, "USB Video Mode");
    lv_obj_set_align(_GUITaskState.UVCOverlayLabel, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(_GUITaskState.UVCOverlayLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(_GUITaskState.UVCOverlayLabel, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_add_flag(_GUITaskState.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);
    _GUITaskState.IsUVCStreaming = false;

    /* Initialize network frame for server streaming */
    _GUITaskState.NetworkFrame.Mutex = xSemaphoreCreateMutex();
    if (_GUITaskState.NetworkFrame.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create NetworkFrame mutex!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    _GUITaskState.SpiMutex = xSemaphoreCreateMutex();
    if (_GUITaskState.SpiMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create SPI bus gate mutex!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        vSemaphoreDelete(_GUITaskState.NetworkFrame.Mutex);
        heap_caps_free(_GUITaskState.ThermalCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    esp_event_handler_register(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, on_Devices_Event_Handler, NULL);
    esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
    esp_event_handler_register(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler, NULL);
    esp_event_handler_register(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY, on_Devices_Task_Event_Handler,
                               NULL);
    esp_event_handler_register(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE, on_Devices_Task_Event_Handler,
                               NULL);
    esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVED, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, on_GUI_Task_Event_Handler, NULL);
    esp_event_handler_register(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler, NULL);
    esp_event_handler_register(LEPTON_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Task_Event_Handler, NULL);
    esp_event_handler_register(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_COMPLETE, on_Camera_Task_Event_Handler, NULL);
    esp_event_handler_register(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_FAILED, on_Camera_Task_Event_Handler, NULL);

    _GUITaskState.SaveNextFrameRequested = false;
    _GUITaskState.ShowCameraView = false;
    _GUITaskState.ShowCrosshair = false;
    _GUITaskState.ShowROI = false;
    _GUITaskState.LongPressHandled = false;
    _GUITaskState.PrevJoyCenterLongPress = false;
    _GUITaskState.IsInitialized = true;

    return ESP_OK;
}

void GUI_Task_Deinit(void)
{
    if (_GUITaskState.IsInitialized == false) {
        return;
    }

    esp_event_handler_unregister(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, on_Devices_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(USB_EVENTS, ESP_EVENT_ANY_ID, on_USB_Event_Handler);
    esp_event_handler_unregister(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_BATTERY, on_Devices_Task_Event_Handler);
    esp_event_handler_unregister(DEVICES_TASK_EVENTS, DEVICES_TASK_EVENT_RESPONSE_TEMPERATURE,
                                 on_Devices_Task_Event_Handler);
    esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVED, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(GUI_TASK_EVENTS, GUI_TASK_EVENT_IMAGE_SAVE_FAILED, on_GUI_Task_Event_Handler);
    esp_event_handler_unregister(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, on_Settings_Event_Handler);
    esp_event_handler_unregister(LEPTON_TASK_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Task_Event_Handler);
    esp_event_handler_unregister(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_COMPLETE, on_Camera_Task_Event_Handler);
    esp_event_handler_unregister(CAMERA_TASK_EVENTS, CAMERA_TASK_EVENT_INIT_FAILED, on_Camera_Task_Event_Handler);

    ui_destroy();

    GUI_Helper_Deinit(&_GUITaskState);

    if (_GUITaskState.NetworkFrame.Mutex != NULL) {
        vSemaphoreDelete(_GUITaskState.NetworkFrame.Mutex);
        _GUITaskState.NetworkFrame.Mutex = NULL;
    }

    if (_GUITaskState.SpiMutex != NULL) {
        vSemaphoreDelete(_GUITaskState.SpiMutex);
        _GUITaskState.SpiMutex = NULL;
    }

    _GUITaskState.Display = NULL;
    _GUITaskState.IsInitialized = false;
}

esp_err_t GUI_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_GUITaskState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_GUITaskState.IsRunning) {
        ESP_LOGW(TAG, "Task already running");

        return ESP_OK;
    }

    _GUITaskState.IsRunning = true;

    ESP_LOGD(TAG, "Starting GUI Task");

    Error = xTaskCreatePinnedToCore(Task_GUI, "Task_GUI", CONFIG_GUI_TASK_STACKSIZE, p_AppContext, CONFIG_GUI_TASK_PRIO,
                                    &_GUITaskState.TaskHandle, CONFIG_GUI_TASK_CORE);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task: 0x%X!", Error);
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t GUI_Task_Stop(void)
{
    if (_GUITaskState.IsRunning == false) {
        return ESP_OK;
    }

    xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_STOP_REQUEST);

    return ESP_OK;
}

bool GUI_Task_IsRunning(void)
{
    return _GUITaskState.IsRunning;
}

lv_indev_t *GUI_Task_GetKeypadIndev(void)
{
    return _GUITaskState.Keypad;
}

esp_err_t GUI_Task_SaveImage(void)
{
    /* Check if filesystem is locked (USB active) */
    if (MemoryManager_IsFilesystemLocked()) {
        ESP_LOGW(TAG, "Cannot save image - USB mode active!");

        return ESP_ERR_INVALID_STATE;
    }

    /* Set flag to trigger save on next frame update */
    _GUITaskState.SaveNextFrameRequested = true;
    ESP_LOGD(TAG, "Image save requested - will capture next frame");

    return ESP_OK;
}

void GUI_Task_SetCameraView(bool Enable)
{
    _GUITaskState.ShowCameraView = Enable;
    xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_VIEW_CHANGED);
}

void GUI_Task_ToggleCameraView(void)
{
    _GUITaskState.ShowCameraView = !_GUITaskState.ShowCameraView;
    xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_VIEW_CHANGED);
}

void GUI_Task_ToggleROI(void)
{
    _GUITaskState.ShowROI = !_GUITaskState.ShowROI;
    xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_THERMAL_ROI_CHANGED);
}