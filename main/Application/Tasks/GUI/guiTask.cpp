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
#include "guiControl.h"
#include "Export/ui.h"
#include "Application/application.h"
#include "Application/Manager/managers.h"
#include "Application/Manager/Network/Server/server.h"
#include "Private/guiHelper.h"
#include "Private/guiImageSave.h"
#include "Private/guiROI.h"
#include "Private/guiCrosshair.h"
#include "UI/ui_messagebox.h"
#include "UI/ui_settings.h"

#include "lepton.h"

ESP_EVENT_DEFINE_BASE(GUI_TASK_EVENTS);

GUI_Task_State_t _GUITaskState;

static const char *TAG = "GUI-Task";

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
            memcpy(&_GUITaskState.TemperatureInfo, p_Data, sizeof(App_Devices_Temperature_t));

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
            if (p_Data == NULL) {
                break;
            }

            SettingsManager_ChangeNotification_t Changed;

            memcpy(&Changed, p_Data, sizeof(SettingsManager_ChangeNotification_t));

            if (Changed.ID == SETTINGS_ID_LEPTON_PALETTE) {
                xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PALETTE_CHANGED);
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

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED | GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

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

            xEventGroupSetBits(_GUITaskState.EventGroup, GUI_TASK_PROVISIONING_STATE_CHANGED | GUI_TASK_WIFI_CONNECTION_STATE_CHANGED);

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

/** @brief              Create temperature gradient canvas for palette visualization.
 *                      Generates a vertical gradient from hot (top of canvas, palette index 255)
 *                      to cold (bottom of canvas, palette index 0) using fictive temperature values
 *                      mapped linearly across the full 256-entry palette LUT.
 *  @param p_Palette    Pointer to the 256-entry RGB888 palette LUT to use for the gradient
 */
static void UI_Canvas_AddTempGradient(const uint8_t (*p_Palette)[3])
{
    /* Generate gradient pixel by pixel. */
    uint16_t *Buffer = reinterpret_cast<uint16_t *>(_GUITaskState.GradientCanvasBuffer);

    for (uint32_t y = 0; y < _GUITaskState.GradientImageDescriptor.header.h; y++) {
        uint32_t Index;

        /* Map y position to palette index using fictive temperature values:
         * top (y=0) -> index 255 (hottest)
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
/** @brief              Redraw the gradient canvas using the palette currently stored in settings
 *                      and invalidate the LVGL image widget so the new colours are displayed.
 *  @note               Must be called from the GUI task only (not thread-safe).
 *  @param PaletteIdx   Index of the palette to use for the gradient (Lepton_Palette_t).
 */
static void GUI_Task_UpdateGradient(uint8_t PaletteIdx)
{
    UI_Canvas_AddTempGradient(Lepton_Palette_Table[PaletteIdx]);
    lv_obj_invalidate(ui_Image_Main_Gradient);
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
 *                  Maps the debounced displayboard input state (buttons) to LVGL events.
 *                  ShortPress consume-once flags generate LV_EVENT_KEY via a one-tick
 *                  PRESSED pulse.  LongPress consume-once flags are injected directly as
 *                  LV_EVENT_LONG_PRESSED on the currently focused group widget, with the
 *                  originating key code passed as the event parameter.
 *  @param p_Indev  Input device handle
 *  @param p_Data   Input device data
 */
static void Keypad_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    DevicesManager_Input_State_t State;

    static const struct {
        uint32_t Key;
        const char *Name;
    } KEY_MAP[] = {
        { GUI_KEYPAD_BTN1, "BTN1" },
        { GUI_KEYPAD_BTN2, "BTN2" },
        { GUI_KEYPAD_BTN3, "BTN3" },
        { GUI_KEYPAD_BTN4, "BTN4" },
    };

    if ((_GUITaskState.AppContext == NULL) || (_GUITaskState.AppContext->InputMutex == NULL)) {
        p_Data->state = LV_INDEV_STATE_RELEASED;

        return;
    }

    /* Read state and consume ShortPress / LongPress flags in one critical section. */
    xSemaphoreTake(_GUITaskState.AppContext->InputMutex, portMAX_DELAY);

    /* Read state and consume ShortPress / LongPress flags in one critical section. */
    memcpy(&State, &_GUITaskState.AppContext->InputState, sizeof(DevicesManager_Input_State_t));
    for (size_t i = 0; i < sizeof(_GUITaskState.AppContext->InputState.Buttons) / sizeof(_GUITaskState.AppContext->InputState.Buttons[0]); i++) {
        _GUITaskState.AppContext->InputState.Buttons[i].ShortPress = false;
        _GUITaskState.AppContext->InputState.Buttons[i].LongPress = false;
    }

    _GUITaskState.AppContext->InputState.Joystick.Center.ShortPress = false;
    _GUITaskState.AppContext->InputState.Joystick.Center.LongPress = false;

    xSemaphoreGive(_GUITaskState.AppContext->InputMutex);

    /* Inject LV_EVENT_LONG_PRESSED for pending long-press flags.
     * The key code is passed as event param so screen handlers can identify the button. */
    lv_group_t *Group = lv_indev_get_group(p_Indev);
    if (Group != NULL) {
        lv_obj_t *Focused = lv_group_get_focused(Group);

        if (Focused != NULL) {
            for (size_t i = 0; i < sizeof(_GUITaskState.AppContext->InputState.Buttons) / sizeof(_GUITaskState.AppContext->InputState.Buttons[0]); i++) {
                if (State.Buttons[i].LongPress == true) {
                    ESP_LOGD(TAG, "Keypad: %s long-press", KEY_MAP[i].Name);
                    lv_obj_send_event(Focused, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)KEY_MAP[i].Key);
                }
            }

            if (State.Joystick.Center.LongPress == true) {
                ESP_LOGD(TAG, "Keypad: JoyCenter long-press");
                lv_obj_send_event(Focused, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)GUI_KEYPAD_JOY_CENTER);
            }
        }
    }

    /* Key state priority (highest to lowest):
     *  1. Button short-presses (BTN1–4): pulse-based, consume-once flag → one tick PRESSED.
     *     LVGL fires LV_EVENT_KEY on the focused widget after the RELEASED tick.
     *  2. JoyCenter short-press: same pulse-based mechanism → LV_KEY_ENTER.
     *  3. Joystick directions (JoyUp/Down/Left/Right): level-based → PRESSED while held.
     *     LVGL handles auto-repeat natively via its configured long_press / repeat timers. */
    p_Data->key = 0;
    p_Data->state = LV_INDEV_STATE_RELEASED;

    for (size_t i = 0; i < sizeof(_GUITaskState.AppContext->InputState.Buttons) / sizeof(_GUITaskState.AppContext->InputState.Buttons[0]); i++) {
        if (State.Buttons[i].ShortPress == true) {
            p_Data->key = KEY_MAP[i].Key;
            p_Data->state = LV_INDEV_STATE_PRESSED;
            ESP_LOGD(TAG, "Keypad: %s short-press", KEY_MAP[i].Name);

            return;
        }
    }

    if (State.Joystick.Center.ShortPress == true) {
        p_Data->key = GUI_KEYPAD_JOY_CENTER;
        p_Data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "Keypad: JoyCenter short-press");

        return;
    }

    if (State.Joystick.Up == true) {
        p_Data->key = GUI_KEYPAD_JOY_UP;
        p_Data->state = LV_INDEV_STATE_PRESSED;
    } else if (State.Joystick.Down == true) {
        p_Data->key = GUI_KEYPAD_JOY_DOWN;
        p_Data->state = LV_INDEV_STATE_PRESSED;
    } else if (State.Joystick.Left == true) {
        p_Data->key = GUI_KEYPAD_JOY_LEFT;
        p_Data->state = LV_INDEV_STATE_PRESSED;
    } else if (State.Joystick.Right == true) {
        p_Data->key = GUI_KEYPAD_JOY_RIGHT;
        p_Data->state = LV_INDEV_STATE_PRESSED;
    }
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

/** @brief              Update the network image buffer with the latest camera frame.
 *  @param p_Buffer     Pointer to the source image buffer (RGB565 format)
 *  @param ImageWidth   Width of the image
 *  @param ImageHeight  Height of the image
 */
static void GUI_UpdateNetworkImage(const uint8_t* p_Buffer, uint32_t ImageWidth, int32_t ImageHeight)
{
    /* Update NetworkRGBBuffer with the camera frame so that HTTP capture and
     * WebSocket clients always receive the same image that is shown on the LCD.
     * UI_Scale_Camera applies the same 180° pre-rotation as the thermal path,
     * so the same reverse-order read is used to undo it. */
    if (xSemaphoreTake(_GUITaskState.NetworkFrame.Mutex, 0) == pdTRUE) {
        uint8_t *Rgb888Dst = _GUITaskState.NetworkRGBBuffer;
        uint32_t Total = ImageWidth * ImageHeight;

        /* Reset watchdog before RGB conversion */
        esp_task_wdt_reset();

        for (uint32_t i = 0; i < Total; i++) {
            uint32_t SrcIdx = Total - 1 - i;
            uint16_t Rgb565 = p_Buffer[(SrcIdx * 2) + 0] | (p_Buffer[(SrcIdx * 2) + 1] << 8);

            uint8_t R = (Rgb565 >> 8) & 0xF8;
            uint8_t G = (Rgb565 >> 3) & 0xFC;
            uint8_t B = (Rgb565 << 3) & 0xF8;

            Rgb888Dst[(i * 3) + 0] = R;
            Rgb888Dst[(i * 3) + 1] = G;
            Rgb888Dst[(i * 3) + 2] = B;
        }

        _GUITaskState.NetworkFrame.Buffer = _GUITaskState.NetworkRGBBuffer;
        _GUITaskState.NetworkFrame.Width = static_cast<uint16_t>(ImageWidth);
        _GUITaskState.NetworkFrame.Height = static_cast<uint16_t>(ImageHeight);
        _GUITaskState.NetworkFrame.Timestamp = esp_timer_get_time() / 1000;
        _GUITaskState.NetworkFrame.RawBuffer = NULL;

        xSemaphoreGive(_GUITaskState.NetworkFrame.Mutex);

        /* Reset watchdog after RGB conversion */
        esp_task_wdt_reset();

        if (WebSocket_HasClients()) {
            Server_NotifyClients();
        }
    }
}

/** @brief              GUI Task main function.
 *  @param p_Parameters Task parameters
 */
void Task_GUI(void *p_Parameters)
{
    uint32_t Timeout;
    App_Context_t *App_Context;

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
    lv_label_set_text(ui_Label_Splash_StatusText, "Booting...");
    do {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_GUITaskState.EventGroup);
        if (EventBits & GUI_TASK_CAMERA_READY) {
            lv_label_set_text(ui_Label_Splash_StatusText, "Camera ready");
            lv_bar_set_value(ui_ProgressBar_Splash_Loading, lv_bar_get_value(ui_ProgressBar_Splash_Loading) + 50, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_READY);
        } else if (EventBits & GUI_TASK_LEPTON_READY) {
            lv_label_set_text(ui_Label_Splash_StatusText, "Lepton ready");
            lv_bar_set_value(ui_ProgressBar_Splash_Loading, lv_bar_get_value(ui_ProgressBar_Splash_Loading) + 50, LV_ANIM_ON);

            GUI_Helper_InitTouch(&_GUITaskState, Touch_LVGL_ReadCallback);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_READY);
        } else if (EventBits & GUI_TASK_LEPTON_ERROR) {
            lv_label_set_text(ui_Label_Splash_StatusText, "Lepton Error");
            lv_bar_set_value(ui_ProgressBar_Splash_Loading, 99, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_LEPTON_ERROR);
        } else if (EventBits & GUI_TASK_CAMERA_ERROR) {
            lv_label_set_text(ui_Label_Splash_StatusText, "Camera Error");
            lv_bar_set_value(ui_ProgressBar_Splash_Loading, 99, LV_ANIM_ON);

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_ERROR);
        } else if (Timeout >= 30000) {
            break;
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
        Timeout += 100;
    } while (lv_bar_get_value(ui_ProgressBar_Splash_Loading) < lv_bar_get_max_value(ui_ProgressBar_Splash_Loading));

    lv_disp_load_scr(ui_Main);

    /* Process layout changes after loading new screen. */
    lv_timer_handler();

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_APP_STARTED, NULL, 0, pdMS_TO_TICKS(500));

    GUI_ROI_Load();

    GUI_Update_Info();

    GUI_Crosshair_Init();

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

    /* Cache label dark/light state: avoids calling lv_obj_set_style_text_color() (and its
     * internal lv_obj_invalidate()) every frame when the luminance threshold has not changed.
     * Initialised to true (= bright background assumed, default text colour black) so that the
     * very first dark-background frame triggers an update and sets the colour to white
     * immediately, without waiting for a luminance transition. */
    bool SceneLabelIsDark[5] = { true, true, true, true, true };

    /* Cache temperature scale label strings: avoids calling lv_label_set_text() (and its
     * internal lv_obj_invalidate()) when the formatted value is identical to the previous frame. */
    char TempScaleMaxBuf[16] = { 0 };
    char TempScaleMinBuf[16] = { 0 };

    while (_GUITaskState.IsRunning) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Process new thermal frame. */
        if (_GUITaskState.ShowCameraView == false) {
            App_Lepton_Frame_t LeptonFrame;

            if (xQueueReceive(App_Context->Lepton_FrameQueue, &LeptonFrame, 0) == pdTRUE) {
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
                Dst = _GUITaskState.ImageCanvasBuffer;
                ImageWidth = lv_obj_get_width(ui_Image_Main_Image);
                ImageHeight = lv_obj_get_height(ui_Image_Main_Image);

                /* Update crosshair overlay label positions every frame.
                 * ui_Label_Main_Thermal_Crosshair and ui_Label_Main_Thermal_PixelTemperature are
                 * children of ui_Container_Main_Thermal_Crosshair (100×50), not direct children
                 * of ui_Image_Main_Thermal. The container position changes with every joystick move,
                 * so the image-relative coordinates must be recomputed here. */
                int32_t ContainerX = lv_obj_get_x(ui_Container_Main_Thermal_Crosshair);
                int32_t ContainerY = lv_obj_get_y(ui_Container_Main_Thermal_Crosshair);

                SceneLabelX0[3] = ContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Crosshair);
                SceneLabelY0[3] = ContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Crosshair);
                SceneLabelX0[4] = ContainerX + lv_obj_get_x(ui_Label_Main_Thermal_Pixel_Temperature);
                SceneLabelY0[4] = ContainerY + lv_obj_get_y(ui_Label_Main_Thermal_Pixel_Temperature);

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
                    bool IsDark = (SceneLabelAverageLuminance[i] > 128);

                    /* Only update style (and the internal lv_obj_invalidate it triggers) when
                     * the luminance threshold actually crosses the 128 boundary. */
                    if (IsDark != SceneLabelIsDark[i]) {
                        lv_obj_set_style_text_color(SceneLabels[i],
                                                    IsDark ? lv_color_black() : lv_color_white(),
                                                    LV_PART_MAIN);
                        SceneLabelIsDark[i] = IsDark;
                    }
                }

                /* Reset watchdog after image processing */
                esp_task_wdt_reset();

                /* Max temperature (top of gradient) */
                float TempMaxCelsius = (LeptonFrame.Max / 100.0f) - 273.15f;
                snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", TempMaxCelsius);
                if (strncmp(Buffer, TempScaleMaxBuf, sizeof(Buffer)) != 0) {
                    lv_label_set_text(ui_Label_Main_Temp_Scale_Max, Buffer);
                    strncpy(TempScaleMaxBuf, Buffer, sizeof(TempScaleMaxBuf));
                }

                /* Min temperature (bottom of gradient) */
                float TempMinCelsius = (LeptonFrame.Min / 100.0f) - 273.15f;
                snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", TempMinCelsius);
                if (strncmp(Buffer, TempScaleMinBuf, sizeof(Buffer)) != 0) {
                    lv_label_set_text(ui_Label_Main_Temp_Scale_Min, Buffer);
                    strncpy(TempScaleMinBuf, Buffer, sizeof(TempScaleMinBuf));
                }

                /* Trigger LVGL to redraw the image */
                lv_obj_invalidate(ui_Image_Main_Image);
                ESP_LOGD(TAG, "Updated thermal image display (src: %ux%u -> dst: %ux%u)", LeptonFrame.Width, LeptonFrame.Height,
                         ImageWidth, ImageHeight);

                GUI_UpdateNetworkImage(_GUITaskState.ImageCanvasBuffer, ImageWidth, ImageHeight);
            }
        }

        /* Process new visible-light camera frame. */
        if (_GUITaskState.ShowCameraView) {
            if (App_Context->Camera_FrameQueue != NULL) {
                App_Camera_Frame_t CameraFrame;

                if (xQueueReceive(App_Context->Camera_FrameQueue, &CameraFrame, 0) == pdTRUE) {
                    /* Scale camera frame down and render in the thermal canvas */
                    UI_Scale_Camera(CameraFrame.Buffer, CameraFrame.Width, CameraFrame.Height,
                                    _GUITaskState.ImageCanvasBuffer, GUI_IMAGE_CANVAS_WIDTH, GUI_IMAGE_CANVAS_HEIGHT);
                    lv_obj_invalidate(ui_Image_Main_Image);

                    GUI_UpdateNetworkImage(_GUITaskState.ImageCanvasBuffer, GUI_IMAGE_CANVAS_WIDTH, GUI_IMAGE_CANVAS_HEIGHT);
                }
            }
        }

        /* Save the currently displayed frame (thermal or camera) if requested.
         * A dedicated snapshot copy (SaveCanvasBuffer) is used to prevent a data race
         * between Task_GUI overwriting ImageCanvasBuffer and Task_ImageSave reading it.
         */
        if (_GUITaskState.SaveNextFrameRequested) {
            App_Lepton_Frame_t SaveFrame;

            _GUITaskState.SaveNextFrameRequested = false;

            /* Take a snapshot of the current canvas before handing the pointer to Task_ImageSave. */
            memcpy(_GUITaskState.SaveCanvasBuffer, _GUITaskState.ImageCanvasBuffer,
                   GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2);

            SaveFrame.Buffer = _GUITaskState.SaveCanvasBuffer;
            SaveFrame.Width = GUI_IMAGE_CANVAS_WIDTH;
            SaveFrame.Height = GUI_IMAGE_CANVAS_HEIGHT;

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

            snprintf(Buffer, sizeof(Buffer), "%.2f \xC2\xB0""C", _GUITaskState.TemperatureInfo.FPA);
            lv_label_set_text(ui_Label_Info_Lepton_FPA, Buffer);
            snprintf(Buffer, sizeof(Buffer), "%.2f \xC2\xB0""C", _GUITaskState.TemperatureInfo.AUX);
            lv_label_set_text(ui_Label_Info_Lepton_AUX, Buffer);

            Server_SetLeptonTemperatures(_GUITaskState.TemperatureInfo.FPA, _GUITaskState.TemperatureInfo.AUX);

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
                memset(_GUITaskState.ImageCanvasBuffer, 0x00, GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2);
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
                lv_obj_add_flag(ui_Label_Main_Temp_Scale_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Temp_Scale_Min, LV_OBJ_FLAG_HIDDEN);
            } else {
                /* Hide UVC overlay */
                lv_obj_add_flag(_GUITaskState.UVCOverlayLabel, LV_OBJ_FLAG_HIDDEN);

                /* Restore ROI rectangles */
                GUI_ROI_Show();

                /* Restore temperature labels */
                lv_obj_remove_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);

                /* Restore temperature scale labels */
                lv_obj_remove_flag(ui_Label_Main_Temp_Scale_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Temp_Scale_Min, LV_OBJ_FLAG_HIDDEN);
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_UVC_STREAMING_STATE_CHANGED);
        }

        if (EventBits & GUI_TASK_CAMERA_VIEW_CHANGED) {
            /* Leaving thermal view: restore camera overlay */
            if (_GUITaskState.ShowCameraView) {
                bool FlashEnabled;

                memset(_GUITaskState.ImageCanvasBuffer, 0x00, GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2);

                /* Hide temperature overlay and scale labels */
                lv_obj_add_flag(ui_Label_Main_Thermal_Pixel_Temperature, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Thermal_Scene_Mean, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Temp_Scale_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Label_Main_Temp_Scale_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Container_Main_Thermal_Crosshair, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Gradient, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_AGC_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Scene_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Video_Focus_ROI, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_Image_Main_Thermal_Spotmeter_ROI, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text(ui_Label_Main_Button2, "\uF0EB");

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
                lv_obj_remove_flag(ui_Label_Main_Temp_Scale_Max, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Label_Main_Temp_Scale_Min, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui_Image_Main_Gradient, LV_OBJ_FLAG_HIDDEN);

                lv_label_set_text(ui_Label_Main_Button2, "\uE595");

                GUI_Crosshair_Show();
                GUI_ROI_Show();
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_CAMERA_VIEW_CHANGED);
        }

        if (EventBits & GUI_TASK_PALETTE_CHANGED) {
            Settings_Lepton_t LeptonSettings;

            SettingsManager_GetLepton(&LeptonSettings);

            /* Update palette dropdown if the change came from elsewhere (e.g. HTTP API) */
            if (lv_dropdown_get_selected(ui_palette_dropdown) != LeptonSettings.Palette) {
                GUI_Task_UpdateGradient(LeptonSettings.Palette);
                lv_dropdown_set_selected(ui_palette_dropdown, LeptonSettings.Palette);
            }

            xEventGroupClearBits(_GUITaskState.EventGroup, GUI_TASK_PALETTE_CHANGED);
        }

        if (EventBits & GUI_TASK_TEMPERATURE_SENSOR_READY) {
            char Buffer[16];
            Settings_Calibration_t Calibration;
            float AmbientTemp;

            SettingsManager_GetCalibration(&Calibration);

            /* Apply the calibration offset to convert the raw sensor reading to an estimated
             * ambient temperature: ambient = sensor + (RoomTemperature - SensorAtCalibration) */
            AmbientTemp = _GUITaskState.TemperatureInfo.TempSensor +
                          (static_cast<float>(Calibration.RoomTemperature) - Calibration.SensorAtCalibration);

            snprintf(Buffer, sizeof(Buffer), "%.1f \xC2\xB0""C", AmbientTemp);
            lv_label_set_text(ui_Label_Main_Statusbar_Temperatur, Buffer);

            Server_SetDeviceTemperature(AmbientTemp);

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

    _GUITaskState.ImageCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(GUI_IMAGE_CANVAS_WIDTH *
                                                                                GUI_IMAGE_CANVAS_HEIGHT * 2, MALLOC_CAP_SPIRAM));
    _GUITaskState.GradientCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(GUI_GRADIENT_CANVAS_WIDTH *
                                                                                 GUI_IMAGE_CANVAS_HEIGHT * 2, MALLOC_CAP_SPIRAM));
    _GUITaskState.NetworkRGBBuffer = static_cast<uint8_t *>(heap_caps_malloc(GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT
                                                                             * 3, MALLOC_CAP_SPIRAM));
    /* Raw 14-bit network buffer: 160 × 120 × 2 = 38,400 bytes (PSRAM, Lepton native resolution) */
    _GUITaskState.NetworkRawBuffer = static_cast<uint16_t *>(heap_caps_malloc(160 * 120 * sizeof(uint16_t),
                                                                              MALLOC_CAP_SPIRAM));
    _GUITaskState.SaveCanvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT
                                                                             * 2, MALLOC_CAP_SPIRAM));

    if (_GUITaskState.ImageCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate thermal canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.GradientCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate gradient canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.NetworkRGBBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network RGB buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.NetworkRawBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network raw buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);

        return ESP_ERR_NO_MEM;
    }

    if (_GUITaskState.SaveCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate save canvas buffer!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.NetworkRawBuffer);

        return ESP_ERR_NO_MEM;
    }

    _GUITaskState.ImageSaveQueue = xQueueCreate(1, sizeof(App_Lepton_Frame_t));
    if (_GUITaskState.ImageSaveQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create image save queue!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_ERR_NO_MEM);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.NetworkRawBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        return ESP_ERR_NO_MEM;
    }

    Error = xTaskCreatePinnedToCore(Task_ImageSave, "Task_ImgSave", 8192, NULL, CONFIG_GUI_TASK_PRIO - 1,
                                    &_GUITaskState.ImageSaveTaskHandle, 1);
    if (Error != pdPASS) {
        ESP_LOGE(TAG, "Failed to create image save task!");
        APP_DIAG_RECORD(APP_DIAG_SOURCE_TASK_GUI, ESP_FAIL);

        vQueueDelete(_GUITaskState.ImageSaveQueue);

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
        heap_caps_free(_GUITaskState.GradientCanvasBuffer);
        heap_caps_free(_GUITaskState.NetworkRGBBuffer);
        heap_caps_free(_GUITaskState.SaveCanvasBuffer);

        _GUITaskState.ImageSaveQueue = NULL;
        _GUITaskState.ImageCanvasBuffer = NULL;
        _GUITaskState.GradientCanvasBuffer = NULL;
        _GUITaskState.NetworkRGBBuffer = NULL;
        _GUITaskState.SaveCanvasBuffer = NULL;

        return ESP_FAIL;
    }

    /* Initialize buffers with black pixels (RGB565 = 0x0000) */
    memset(_GUITaskState.ImageCanvasBuffer, 0x00, GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2);
    memset(_GUITaskState.GradientCanvasBuffer, 0x00, GUI_GRADIENT_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2);

    /* Now configure the image descriptors with allocated buffers */
    _GUITaskState.ThermalImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITaskState.ThermalImageDescriptor.header.w = GUI_IMAGE_CANVAS_WIDTH;
    _GUITaskState.ThermalImageDescriptor.header.h = GUI_IMAGE_CANVAS_HEIGHT;
    _GUITaskState.ThermalImageDescriptor.data = _GUITaskState.ImageCanvasBuffer;
    _GUITaskState.ThermalImageDescriptor.data_size = GUI_IMAGE_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2;

    _GUITaskState.GradientImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITaskState.GradientImageDescriptor.header.w = GUI_GRADIENT_CANVAS_WIDTH;
    _GUITaskState.GradientImageDescriptor.header.h = GUI_IMAGE_CANVAS_HEIGHT;
    _GUITaskState.GradientImageDescriptor.data = _GUITaskState.GradientCanvasBuffer;
    _GUITaskState.GradientImageDescriptor.data_size = GUI_GRADIENT_CANVAS_WIDTH * GUI_IMAGE_CANVAS_HEIGHT * 2;

    Settings_Lepton_t LeptonSettings;
    SettingsManager_GetLepton(&LeptonSettings);
    GUI_Task_UpdateGradient(LeptonSettings.Palette);

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

        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
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
        heap_caps_free(_GUITaskState.ImageCanvasBuffer);
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
    _GUITaskState.IsInitialized = true;

    GUI_ROI_Hide();

    GUI_Crosshair_Hide();
    
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

void GUI_Task_ActivateROIConfig(void)
{
    /* Skip if RGB view is active or ROI config mode is already active */
    if (_GUITaskState.ShowCameraView || _GUITaskState.ROIConfig.IsActive) {
        return;
    }

    GUI_ROI_Init();
}
