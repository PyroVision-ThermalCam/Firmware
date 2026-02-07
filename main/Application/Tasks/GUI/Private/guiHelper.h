/*
 * guiHelper.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Helper functions for the GUI task.
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

#ifndef GUI_HELPER_H_
#define GUI_HELPER_H_

#include <esp_timer.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_touch_gt911.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lvgl.h>

#include "Application/application.h"
#include "Application/Manager/Network/networkTypes.h"

#define GUI_TASK_STOP_REQUEST               BIT0
#define BATTERY_VOLTAGE_READY               BIT1
#define BATTERY_CHARGING_STATUS_READY       BIT2
#define WIFI_CONNECTION_STATE_CHANGED       BIT3
#define PROVISIONING_STATE_CHANGED          BIT7
#define SD_CARD_STATE_CHANGED               BIT8
#define SD_CARD_MOUNTED                     BIT9
#define SD_CARD_MOUNT_ERROR                 BIT10
#define LEPTON_UPTIME_READY                 BIT11
#define LEPTON_TEMP_READY                   BIT12
#define LEPTON_PIXEL_TEMPERATURE_READY      BIT13
#define LEPTON_CAMERA_READY                 BIT4
#define LEPTON_CAMERA_ERROR                 BIT14
#define LEPTON_SPOTMETER_READY              BIT5
#define LEPTON_SCENE_STATISTICS_READY       BIT6

typedef struct {
    bool isInitialized;
    bool isRunning;
    bool WiFiConnected;
    bool ProvisioningActive;
    bool CardPresent;
    TaskHandle_t TaskHandle;
    TaskHandle_t ImageSaveTaskHandle;
    void *DisplayBuffer1;
    void *DisplayBuffer2;
    i2c_master_bus_handle_t Touch_Bus_Handle;
    esp_timer_handle_t LVGL_TickTimer;
    esp_lcd_panel_handle_t PanelHandle;
    esp_lcd_touch_handle_t TouchHandle;
    esp_lcd_panel_io_handle_t Panel_IO_Handle;
    esp_lcd_panel_io_handle_t Touch_IO_Handle;
    lv_display_t *Display;
    lv_indev_t *Touch;
    lv_img_dsc_t ThermalImageDescriptor;
    lv_img_dsc_t GradientImageDescriptor;
    lv_timer_t *UpdateTimer[4];
    _lock_t LVGL_API_Lock;
    App_Devices_Battery_t BatteryInfo;
    App_Lepton_ROI_Result_t ROIResult;
    App_Lepton_Device_t LeptonDeviceInfo;
    App_Lepton_Temperatures_t LeptonTemperatures;
    App_Context_t *AppContext;
    Network_IP_Info_t IP_Info;
    EventGroupHandle_t EventGroup;
    uint8_t *ThermalCanvasBuffer;
    uint8_t *GradientCanvasBuffer;
    uint8_t *NetworkRGBBuffer;
    uint32_t LeptonUptime;
    float SpotTemperature;
    QueueHandle_t ImageSaveQueue;
    Network_Thermal_Frame_t NetworkFrame;
    bool SaveNextFrameRequested;

#ifdef CONFIG_GUI_TOUCH_DEBUG
    /* Touch debug visualization */
    lv_obj_t *TouchDebugOverlay;
    lv_obj_t *TouchDebugCircle;
    lv_obj_t *TouchDebugLabel;
#endif
} GUI_Task_State_t;

/** @brief                      Initialize the GUI helper functions.
 *  @param p_GUI_Task_State     Pointer to the GUI task state structure.
 *  @param Touch_Read_Callback  LVGL touch read callback function.
 */
esp_err_t GUI_Helper_Init(GUI_Task_State_t *p_GUI_Task_State, lv_indev_read_cb_t Touch_Read_Callback);

/** @brief                      Deinitialize the GUI helper functions.
 *  @param p_GUI_Task_State     Pointer to the GUI task state structure.
 */
void GUI_Helper_Deinit(GUI_Task_State_t *p_GUI_Task_State);

/** @brief          LVGL timer callback to update the clock display.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_ClockUpdate(lv_timer_t *p_Timer);

/** @brief          LVGL timer callback to update the spotmeter display.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_SpotUpdate(lv_timer_t *p_Timer);

/** @brief          LVGL timer callback to request spotmeter data update.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_SpotmeterUpdate(lv_timer_t *p_Timer);

/** @brief          LVGL timer callback to request scene statistics data update.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_SceneStatisticsUpdate(lv_timer_t *p_Timer);

#endif /* GUI_HELPER_H_ */