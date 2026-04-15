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

#define GUI_TASK_STOP_REQUEST                       BIT0
#define GUI_TASK_BATTERY_STATUS_CHANGED             BIT1
#define GUI_TASK_UVC_STREAMING_STATE_CHANGED        BIT2
#define GUI_TASK_WIFI_CONNECTION_STATE_CHANGED      BIT3
#define GUI_TASK_LEPTON_READY                       BIT4
#define GUI_TASK_LEPTON_SCENE_STATISTICS_READY      BIT6
#define GUI_TASK_PROVISIONING_STATE_CHANGED         BIT7
#define GUI_TASK_SD_CARD_STATE_CHANGED              BIT8
#define GUI_TASK_LEPTON_UPTIME_READY                BIT11
#define GUI_TASK_LEPTON_TEMPERATURE_READY           BIT12
#define GUI_TASK_LEPTON_PIXEL_TEMPERATURE_READY     BIT13
#define GUI_TASK_LEPTON_ERROR                       BIT14
#define GUI_TASK_CAMERA_ERROR                       BIT15
#define GUI_TASK_CAMERA_READY                       BIT16
#define GUI_TASK_TEMPERATURE_SENSOR_READY           BIT17
#define GUI_TASK_CAMERA_VIEW_CHANGED                BIT18
#define GUI_TASK_SCREEN_REFRESH_REQUIRED            BIT19

/** @brief Internal runtime state of the GUI task.
 *         Aggregates all LVGL handles, display and touch panel handles, FreeRTOS primitives,
 *         canvas/network frame buffers, and the latest sensor/thermal data snapshots used by
 *         the GUI task loop and its associated helper functions.
 */
typedef struct {
    bool IsInitialized;                                     /**< true after GUI_Task_Init() has completed successfully. */
    bool IsRunning;                                         /**< true while the main GUI FreeRTOS task is executing. */
    bool WiFiConnected;                                     /**< true while a WiFi station connection is active. */
    bool ProvisioningActive;                                /**< true while WiFi provisioning is in progress. */
    bool CardPresent;                                       /**< true while an SD card is mounted and accessible. */
    bool SaveNextFrameRequested;                            /**< true when the next rendered frame should be saved as PNG. */
    bool IsUVCStreaming;                                    /**< true while a UVC host is actively receiving frames. */
    bool ShowCameraView;                                    /**< true while the visible-light camera image is shown in place of the thermal image. */
    bool PrevJoyCenter;                                     /**< Previous state of the joystick center button for edge detection. */
    bool PrevJoyUp;                                         /**< Previous joystick up state; used for crosshair movement rising-edge detection. */
    bool PrevJoyDown;                                       /**< Previous joystick down state; used for crosshair movement rising-edge detection. */
    bool PrevJoyLeft;                                       /**< Previous joystick left state; used for crosshair movement rising-edge detection. */
    bool PrevJoyRight;                                      /**< Previous joystick right state; used for crosshair movement rising-edge detection. */
    bool CrosshairVisible;                                  /**< true when the crosshair overlay is currently active in the live-view. */
    bool JoyCenterLongFired;                                /**< true after the long-press crosshair toggle has already fired for the current hold; prevents repeated toggling. */
    TickType_t JoyCenterHeldSince;                          /**< Tick at which JoyCenter went high; 0 when not pressed. */
    TickType_t
    JoyDirHeldSince;                             /**< Tick when any joystick direction first went active; 0 when released. */
    TickType_t
    JoyDirLastMoveTick;                          /**< Tick of the most recent crosshair move step; 0 when no direction held. */
    TaskHandle_t TaskHandle;                                /**< FreeRTOS handle of the main GUI task; NULL before start. */
    TaskHandle_t
    ImageSaveTaskHandle;                       /**< FreeRTOS handle of the background image-save task; NULL before init. */
    void *DisplayBuffer1;                                   /**< First PSRAM display frame buffer used by the LCD driver. */
    void *DisplayBuffer2;                                   /**< Second PSRAM display frame buffer used by the LCD driver. */
    i2c_master_bus_handle_t
    Touch_Bus_Handle;               /**< I2C master bus handle shared with the GT911 touch controller. */
    esp_timer_handle_t LVGL_TickTimer;                      /**< ESP timer calling lv_tick_inc() at 1 ms intervals. */
    esp_lcd_panel_handle_t PanelHandle;                     /**< Handle of the ILI9341 LCD panel; NULL before init. */
    esp_lcd_touch_handle_t TouchHandle;                     /**< Handle of the GT911 touch controller; NULL before init. */
    esp_lcd_panel_io_handle_t Panel_IO_Handle;              /**< Panel I/O (SPI) handle used to command the ILI9341. */
    esp_lcd_panel_io_handle_t
    Touch_IO_Handle;              /**< Panel I/O (I2C) handle used to communicate with the GT911. */
    lv_obj_t *UVCOverlayLabel;                              /**< LVGL label object shown as UVC-active overlay; NULL when hidden. */
    lv_display_t
    *Display;                                  /**< LVGL display handle bound to the ILI9341; NULL before init. */
    lv_indev_t
    *Touch;                                      /**< LVGL input device handle for the GT911 touch screen; NULL before init. */
    lv_indev_t
    *Keypad;                                     /**< LVGL input device handle for the physical keypad; NULL before init. */
    lv_img_dsc_t
    ThermalImageDescriptor;                    /**< LVGL image descriptor pointing to the latest thermal RGB canvas. */
    lv_img_dsc_t
    GradientImageDescriptor;                   /**< LVGL image descriptor pointing to the colour-gradient bar canvas. */
    lv_timer_t
    *UpdateTimer[5];                             /**< Array of periodic LVGL timers driving UI element updates. */
    _lock_t LVGL_API_Lock;                                  /**< Lock serialising LVGL API calls from multiple FreeRTOS tasks. */
    App_Devices_Battery_t BatteryInfo;                      /**< Latest battery voltage, percentage and charging state. */
    App_Devices_Temperature_t
    TemperatureInfo;              /**< Latest ambient/housing temperature readings from the devices task. */
    App_Lepton_ROI_Result_t ROIResult;                      /**< Latest Lepton ROI statistics (min/max/avg temperatures). */
    App_Lepton_Device_t LeptonDeviceInfo;                   /**< Lepton model, firmware version, and capability flags. */
    App_Lepton_Temperatures_t
    LeptonTemperatures;           /**< Lepton scene temperature statistics used by the thermal overlay. */
    App_Context_t
    *AppContext;                              /**< Pointer to the shared application context; valid after Task_Start(). */
    EventGroupHandle_t
    EventGroup;                          /**< Event group used for intra-task and inter-task synchronisation. */
    uint8_t *ThermalCanvasBuffer;                           /**< PSRAM canvas buffer for the scaled thermal RGB image. */
    uint8_t *GradientCanvasBuffer;                          /**< PSRAM canvas buffer for the colour-gradient bar image. */
    uint8_t *NetworkRGBBuffer;                              /**< PSRAM RGB buffer transmitted to connected WebSocket clients. */
    uint8_t *SaveCanvasBuffer;                              /**< PSRAM snapshot buffer holding a copy of ThermalCanvasBuffer at save-request time; prevents data races with Task_ImageSave. */
    uint32_t LeptonUptime;                                  /**< Lepton camera uptime in seconds, updated on each frame event. */
    uint32_t PrevBtnKey;                                    /**< Previous button key state for edge detection. */
    float SpotTemperature;                                  /**< Current spotmeter temperature in degrees Celsius. */
    QueueHandle_t
    ImageSaveQueue;                           /**< Queue carrying image-save requests to the background save task. */
    SemaphoreHandle_t
    SpiMutex;                           /**< Mutex serialising SPI3 bus access between the LCD flush path and SD card writes in Task_ImageSave. */
    Network_IP_Info_t IP_Info;                              /**< Current device IP address information (STA or AP mode). */
    Network_Thermal_Frame_t
    NetworkFrame;                   /**< Staging buffer for the thermal frame payload sent over WebSocket. */
} GUI_Task_State_t;

/** @brief                      Initialize the GUI helper functions.
 *                              Sets up the LCD panel, LVGL display, LVGL timer, and � if
 *                              called before the GT911 I2C address is claimed � the touch
 *                              I2C panel I/O handle. Keypad and touch LVGL input devices
 *                              are registered in separate calls.
 *  @note                       After this call, GUI_Helper_InitKeypad() must still be called
 *                              to activate keypad navigation. Touch is activated later via
 *                              GUI_Helper_InitTouch() once the Lepton camera has booted.
 *  @param p_GUITaskState       Pointer to the GUI task state structure.
 *  @param Touch_Read_Callback  LVGL touch read callback function.
 *  @param Display_Flush_CB     LVGL display flush callback function.
 *  @return                     ESP_OK on success
 *                              ESP_ERR_INVALID_ARG if p_GUITaskState is NULL
 *                              ESP_ERR_NO_MEM if display buffer allocation fails
 *                              ESP_FAIL if LCD panel or LVGL initialization fails
 */
esp_err_t GUI_Helper_Init(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Touch_Read_Callback,
                          lv_display_flush_cb_t Display_Flush_CB);

/** @brief                      Initialize the GT911 touch controller and register the LVGL input device.
 *                              Must be called AFTER the Lepton camera has booted (LEPTON_CAMERA_READY event)
 *                              to avoid I2C bus interference that prevents CCI_WaitForBoot from succeeding.
 *                              GT911 is intentionally kept in hardware reset (CONFIG_TOUCH_RST = LOW) until
 *                              this function is called.
 *  @note                       Safe to call even if Touch_IO_Handle was never set up - the function
 *                              checks preconditions and returns gracefully. On initialization failure
 *                              the system continues without touch functionality.
 *  @param p_GUITaskState       Pointer to the GUI task state structure.
 *  @param Touch_Read_Callback  LVGL touch read callback function.
 *  @return                     ESP_OK on success or if GT911 is unavailable (non-fatal)
 *                              ESP_ERR_INVALID_ARG if p_GUITaskState is NULL
 *                              ESP_ERR_INVALID_STATE if Touch_IO_Handle is not initialized
 */
esp_err_t GUI_Helper_InitTouch(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Touch_Read_Callback);

/** @brief                      Initialize the keypad LVGL input device for joystick and button navigation.
 *  @param p_GUITaskState       Pointer to the GUI task state structure.
 *  @param Keypad_Read_Callback LVGL keypad read callback function.
 *  @return                     ESP_OK on success
 *                              ESP_ERR_INVALID_ARG if p_GUITaskState is NULL
 */
esp_err_t GUI_Helper_InitKeypad(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Keypad_Read_Callback);

/** @brief                  Deinitialize the GUI helper functions.
 *                          Deletes the LVGL tick timer, unregisters LVGL input devices,
 *                          and destroys the LCD panel and I/O handles.
 *  @note                   Must be called only after the GUI task has been stopped.
 *  @param p_GUITaskState   Pointer to the GUI task state structure.
 */
void GUI_Helper_Deinit(GUI_Task_State_t *p_GUITaskState);

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