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

#define GUI_TASK_STOP_REQUEST                   BIT0
#define GUI_TASK_BATTERY_STATUS_CHANGED         BIT1
#define GUI_TASK_WIFI_CONNECTION_STATE_CHANGED  BIT3
#define GUI_TASK_PROVISIONING_STATE_CHANGED     BIT7
#define GUI_TASK_SD_CARD_STATE_CHANGED          BIT8
#define GUI_TASK_LEPTON_UPTIME_READY            BIT11
#define GUI_TASK_LEPTON_TEMP_READY              BIT12
#define GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY BIT13
#define GUI_TASK_LEPTON_CAMERA_READY            BIT4
#define GUI_TASK_LEPTON_CAMERA_ERROR            BIT14
#define GUI_TASK_LEPTON_SCENE_STATISTICS_READY  BIT6
#define GUI_TASK_UVC_STREAMING_STATE_CHANGED    BIT15

typedef struct {
    bool isInitialized;
    bool isRunning;
    bool WiFiConnected;
    bool ProvisioningActive;
    bool CardPresent;
    bool SaveNextFrameRequested;
    bool isUVCStreaming;
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
    lv_obj_t *UVCOverlayLabel;
    lv_display_t *Display;
    lv_indev_t *Touch;
    lv_img_dsc_t ThermalImageDescriptor;
    lv_img_dsc_t GradientImageDescriptor;
    lv_timer_t *UpdateTimer[6];
    _lock_t LVGL_API_Lock;
    App_Devices_Battery_t BatteryInfo;
    App_Lepton_ROI_Result_t ROIResult;
    App_Lepton_Device_t LeptonDeviceInfo;
    App_Lepton_Temperatures_t LeptonTemperatures;
    App_Context_t *AppContext;
    EventGroupHandle_t EventGroup;
    uint8_t *ThermalCanvasBuffer;
    uint8_t *GradientCanvasBuffer;
    uint8_t *NetworkRGBBuffer;
    uint32_t LeptonUptime;
    float SpotTemperature;
    QueueHandle_t ImageSaveQueue;
    Network_IP_Info_t IP_Info;
    Network_Thermal_Frame_t NetworkFrame;

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

/** @brief                      Initialize the GT911 touch controller and register the LVGL input device.
 *                              Must be called AFTER the Lepton camera has booted (LEPTON_CAMERA_READY event)
 *                              to avoid I2C bus interference that prevents CCI_WaitForBoot from succeeding.
 *                              GT911 is intentionally kept in hardware reset (CONFIG_TOUCH_RST = LOW) until
 *                              this function is called.
 *  @note                       Safe to call even if Touch_IO_Handle was never set up - the function
 *                              checks preconditions and returns gracefully. On initialization failure
 *                              the system continues without touch functionality.
 *  @param p_GUI_Task_State     Pointer to the GUI task state structure.
 *  @param Touch_Read_Callback  LVGL touch read callback function.
 *  @return                     ESP_OK on success or if GT911 is unavailable (non-fatal)
 *                              ESP_ERR_INVALID_ARG if p_GUI_Task_State is NULL
 *                              ESP_ERR_INVALID_STATE if Touch_IO_Handle is not initialized
 */
esp_err_t GUI_Helper_InitTouch(GUI_Task_State_t *p_GUI_Task_State, lv_indev_read_cb_t Touch_Read_Callback);

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

/** @brief          LVGL timer callback to request scene statistics data update.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_SceneStatisticsUpdate(lv_timer_t *p_Timer);

/** @brief          LVGL timer callback to request RAM usage update.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_RAMUpdate(lv_timer_t *p_Timer);

/** @brief          LVGL timer callback to request Flash usage update.
 *  @param p_Timer  Pointer to the LVGL timer structure.
 */
void GUI_Helper_Timer_MemoryUpdate(lv_timer_t *p_Timer);

#endif /* GUI_HELPER_H_ */