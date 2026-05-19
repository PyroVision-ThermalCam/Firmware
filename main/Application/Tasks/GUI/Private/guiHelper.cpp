/*
 * guiHelper.cpp
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

#include <esp_log.h>

#include "guiHelper.h"
#include "Application/app_types.h"
#include "../Export/ui.h"
#include "../UI/ui_settings.h"

#if defined(CONFIG_LCD_SPI2_HOST)
#define LCD_SPI_HOST                        SPI2_HOST
#elif defined(CONFIG_LCD_SPI3_HOST)
#define LCD_SPI_HOST                        SPI3_HOST
#else
#error "No SPI host defined for LCD!"
#endif

/* Full-screen render buffer (CONFIG_GUI_WIDTH × CONFIG_GUI_HEIGHT × 2 = 153,600 bytes per buffer) in PSRAM.
 *
 * With a buffer large enough to hold the entire display, LVGL renders the complete dirty area
 * (e.g. the 240 × 180 thermal/camera canvas = 86,400 bytes) in a single pass and calls the
 * flush callback exactly ONCE per invalidated region.  The previous 1/10-screen buffer
 * (30,720 bytes) forced ~4 partial flush iterations for the canvas, each incurring a full
 * vTaskDelay(10 ms) stall — ~60 ms total per frame.  One flush reduces that to ~5 ms stall.
 *
 * IMPORTANT: The SPI driver allocates (by default) an internal DMA bounce buffer for EACH queued transaction
 * (chunk_size = CONFIG_SPI_TRANSFER_SIZE = 4096 bytes each). The trans_queue_depth MUST be
 * kept low (currently 3) to limit simultaneous bounce buffer allocations (3 * 4096 = 12 KB),
 * because internal DMA RAM is scarce (~28 KB free after USB init). A higher queue depth
 * (e.g. 10 = 8 chunks * 4096 = 32 KB) would exceed free internal DMA RAM and cause
 * ESP_ERR_NO_MEM in spi_device_queue_trans, freezing the display.
 * Increasing CONFIG_SPI_TRANSFER_SIZE to 8192 is safe (3 × 8192 = 24 KB < 28 KB) and halves
 * the number of SPI transactions per flush.
 */
#define GUI_DRAW_BUFFER_SIZE                (CONFIG_GUI_WIDTH * CONFIG_GUI_HEIGHT * sizeof(uint16_t))

/** @brief          LCD color transfer done callback (called from ISR context after DMA transfer completes).
 *  @param PanelIO  Panel IO handle
 *  @param p_Edata  Event data (unused)
 *  @param p_UserCtx User context (pointer to LVGL display)
 */
static IRAM_ATTR bool on_lcd_color_trans_done(esp_lcd_panel_io_handle_t PanelIO,
                                              esp_lcd_panel_io_event_data_t *p_Edata,
                                              void *p_UserCtx)
{
    (void)PanelIO;
    (void)p_Edata;

    lv_display_t *p_Display = static_cast<lv_display_t *>(p_UserCtx);
    lv_display_flush_ready(p_Display);

    return false;
}

static const esp_lcd_panel_io_callbacks_t _GUI_Panel_Callbacks = {
    .on_color_trans_done = on_lcd_color_trans_done,
};

static const esp_lcd_panel_dev_config_t _GUI_Panel_Config = {
    .reset_gpio_num = CONFIG_LCD_RST,
    .color_space = ESP_LCD_COLOR_SPACE_BGR,
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    .bits_per_pixel = 16,
    .flags = {
        .reset_active_high = 0,
    },
    .vendor_config = NULL,
};

static const esp_lcd_panel_io_spi_config_t _GUI_Panel_IO_Config = {
    .cs_gpio_num = CONFIG_LCD_CS,
    .dc_gpio_num = CONFIG_LCD_DC,
    .spi_mode = 0,
    .pclk_hz = CONFIG_LCD_CLOCK,
    /* NOTE: This parameter must be set to a lower value (e.g., 3) to avoid exceeding internal DMA RAM when not using the `psram_mode` flag.
     */
    .trans_queue_depth = 3,
    .on_color_trans_done = NULL,
    .user_ctx = NULL,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .cs_ena_pretrans = 0,
    .cs_ena_posttrans = 0,
    .flags = {
        .dc_high_on_cmd = 0,
        .dc_low_on_data = 0,
        .dc_low_on_param = 0,
        .octal_mode = 0,
        .quad_mode = 0,
        .sio_mode = 0,
        .lsb_first = 0,
        .cs_high_active = 0,
        .psram_mode = 1,
    },
};

static const esp_lcd_panel_io_i2c_config_t _GUI_Touch_IO_Config = {
#if((CONFIG_TOUCH_IRQ != -1) && (CONFIG_TOUCH_RST != -1))
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
#else
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
#endif
    .on_color_trans_done = NULL,
    .user_ctx = NULL,
    .control_phase_bytes = 1,
    .dc_bit_offset = 0,
    .lcd_cmd_bits = 16,
    .lcd_param_bits = 0,
    .flags =
    {
        .dc_low_on_data = 0,
        .disable_control_phase = 1,
    },
    .scl_speed_hz = CONFIG_TOUCH_CLOCK,
    .transaction_timeout_ms = 100,
};

static esp_lcd_touch_io_gt911_config_t _GUI_Touch_GT911_Config = {
    .dev_addr = static_cast<uint8_t>(_GUI_Touch_IO_Config.dev_addr),
};

static const esp_lcd_touch_config_t _GUI_Touch_Config = {
    .x_max = CONFIG_GUI_WIDTH,
    .y_max = CONFIG_GUI_HEIGHT,
    .rst_gpio_num = static_cast<gpio_num_t>(CONFIG_TOUCH_RST),
    .int_gpio_num = static_cast<gpio_num_t>(CONFIG_TOUCH_IRQ),
    .levels = {
        .reset = 0,
        .interrupt = 0,
    },
    .flags = {
        .swap_xy = 1,
        .mirror_x = 0,
        .mirror_y = 0,
    },
    .process_coordinates = NULL,
    .interrupt_callback = NULL,
    .user_data = NULL,
    .driver_data = &_GUI_Touch_GT911_Config,
};

static const char *TAG = "GUI-Helper";

/** @brief          LVGL tick timer callback for GUI task.
 *  @param p_Arg    User data (pointer to GUI_Task_State_t)
 */
static void GUI_LVGL_TickTimer_CB(void *p_Arg)
{
    (void)p_Arg;

    lv_tick_inc(CONFIG_GUI_LVGL_TICK_PERIOD_MS);
}

esp_err_t GUI_Helper_Init(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Touch_Read_Callback,
                          lv_display_flush_cb_t Display_Flush_CB)
{
    ESP_LOGD(TAG, "Create I2C bus for touch controller...");

    p_GUITaskState->Touch_Bus_Handle = DevicesManager_GetTouchI2CBusHandle();

    ESP_LOGD(TAG, "Create panel IO...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_SPI_HOST), &_GUI_Panel_IO_Config,
                                             &p_GUITaskState->Panel_IO_Handle));
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(p_GUITaskState->Touch_Bus_Handle, &_GUI_Touch_IO_Config,
                                             &p_GUITaskState->Touch_IO_Handle));

    ESP_LOGD(TAG, "Initialize LVGL library and display...");
    lv_init();
    p_GUITaskState->Display = lv_display_create(CONFIG_GUI_WIDTH, CONFIG_GUI_HEIGHT);

    ESP_LOGD(TAG, "Register LCD panel IO callbacks...");
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(p_GUITaskState->Panel_IO_Handle, &_GUI_Panel_Callbacks,
                                                              p_GUITaskState->Display));

    ESP_LOGD(TAG, "Install ILI9341 panel driver...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(p_GUITaskState->Panel_IO_Handle, &_GUI_Panel_Config,
                                              &p_GUITaskState->PanelHandle));
    ESP_LOGD(TAG, " ILI9341 panel driver installed");

    ESP_LOGD(TAG, "Reset the panel...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(p_GUITaskState->PanelHandle));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel reset complete");

    ESP_LOGD(TAG, "Initialize the panel...");
    ESP_ERROR_CHECK(esp_lcd_panel_init(p_GUITaskState->PanelHandle));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel initialized");

    ESP_LOGD(TAG, "Configure panel for landscape mode...");
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(p_GUITaskState->PanelHandle, true));
    ESP_LOGD(TAG, " Panel swap_xy enabled for landscape");

    ESP_LOGD(TAG, "Configure panel mirroring...");
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(p_GUITaskState->PanelHandle, true, true));
    ESP_LOGD(TAG, " Panel mirroring configured (180 degree rotation)");

    ESP_LOGD(TAG, "Turn ON the panel display...");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(p_GUITaskState->PanelHandle, true));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel display turned ON");

    lv_display_set_flush_cb(p_GUITaskState->Display, Display_Flush_CB);
    lv_display_set_user_data(p_GUITaskState->Display, p_GUITaskState->PanelHandle);

    p_GUITaskState->DisplayBuffer1 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    p_GUITaskState->DisplayBuffer2 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    ESP_LOGD(TAG, "Allocated LVGL buffers: %d bytes each in PSRAM", GUI_DRAW_BUFFER_SIZE);
    if ((p_GUITaskState->DisplayBuffer1 == NULL) || (p_GUITaskState->DisplayBuffer2 == NULL)) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers!");

        return ESP_ERR_NO_MEM;
    }

    lv_display_set_buffers(p_GUITaskState->Display,
                           p_GUITaskState->DisplayBuffer1,
                           p_GUITaskState->DisplayBuffer2,
                           GUI_DRAW_BUFFER_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* GT911 touch controller is intentionally NOT initialized here.
     * GPIO CONFIG_TOUCH_RST (GPIO47) was held LOW by DevicesManager_Init, keeping the GT911
     * in hardware reset. Releasing GT911 (RST HIGH) while the Lepton is booting causes
     * I2C bus activity that resets the Lepton's internal CCI register pointer, preventing
     * a successful CCI_WaitForBoot.
     *
     * GT911 is initialized later via GUI_Helper_InitTouch(), called from the GUI task
     * once the LEPTON_CAMERA_READY event is received.
     */
    p_GUITaskState->TouchHandle = NULL;
    p_GUITaskState->Touch = NULL;

    const esp_timer_create_args_t LVGL_TickTimer_args = {
        .callback = &GUI_LVGL_TickTimer_CB,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };

    p_GUITaskState->EventGroup = xEventGroupCreate();
    if (p_GUITaskState->EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create GUI event group!");

        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_timer_create(&LVGL_TickTimer_args, &p_GUITaskState->LVGL_TickTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(p_GUITaskState->LVGL_TickTimer, CONFIG_GUI_LVGL_TICK_PERIOD_MS * 1000));

    p_GUITaskState->UpdateTimer[0] = lv_timer_create(GUI_Helper_Timer_ClockUpdate, 100, NULL);
    p_GUITaskState->UpdateTimer[1] = lv_timer_create(GUI_Helper_Timer_SpotUpdate, 2000, NULL);
    p_GUITaskState->UpdateTimer[2] = lv_timer_create(GUI_Helper_Timer_SceneStatisticsUpdate, 5000, NULL);
    p_GUITaskState->UpdateTimer[3] = lv_timer_create(GUI_Helper_Timer_RAMUpdate, 10000, NULL);
    p_GUITaskState->UpdateTimer[4] = lv_timer_create(GUI_Helper_Timer_MemoryUpdate, 10000, NULL);
    p_GUITaskState->UpdateTimer[5] = lv_timer_create(GUI_Helper_Timer_UptimeUpdate, 1000, NULL);
    p_GUITaskState->UpdateTimer[6] = lv_timer_create(GUI_Helper_Timer_DistanceUpdate, 1000, NULL);
    p_GUITaskState->UpdateTimer[7] = lv_timer_create(GUI_Helper_Timer_LeptonUpdate, 10000, NULL);

    _lock_init(&p_GUITaskState->LVGL_API_Lock);

    return ESP_OK;
}

void GUI_Helper_Deinit(GUI_Task_State_t *p_GUITaskState)
{
    if (p_GUITaskState->IsInitialized == false) {
        return;
    }

    for (uint32_t i = 0; i < sizeof(p_GUITaskState->UpdateTimer) / sizeof(p_GUITaskState->UpdateTimer[0]); i++) {
        if (p_GUITaskState->UpdateTimer[i] != NULL) {
            lv_timer_delete(p_GUITaskState->UpdateTimer[i]);
            p_GUITaskState->UpdateTimer[i] = NULL;
        }
    }

    if (p_GUITaskState->LVGL_TickTimer != NULL) {
        esp_timer_stop(p_GUITaskState->LVGL_TickTimer);
        esp_timer_delete(p_GUITaskState->LVGL_TickTimer);
        p_GUITaskState->LVGL_TickTimer = NULL;
    }

    if (p_GUITaskState->EventGroup != NULL) {
        vEventGroupDelete(p_GUITaskState->EventGroup);
        p_GUITaskState->EventGroup = NULL;
    }

    if (p_GUITaskState->Touch != NULL) {
        lv_indev_delete(p_GUITaskState->Touch);
        p_GUITaskState->Touch = NULL;
    }

    if (p_GUITaskState->Keypad != NULL) {
        lv_indev_delete(p_GUITaskState->Keypad);
        p_GUITaskState->Keypad = NULL;
    }

    esp_lcd_touch_del(p_GUITaskState->TouchHandle);

    if (p_GUITaskState->Display != NULL) {
        lv_display_delete(p_GUITaskState->Display);
        p_GUITaskState->Display = NULL;
    }

    if (p_GUITaskState->DisplayBuffer1 != NULL) {
        heap_caps_free(p_GUITaskState->DisplayBuffer1);
        p_GUITaskState->DisplayBuffer1 = NULL;
    }

    if (p_GUITaskState->DisplayBuffer2 != NULL) {
        heap_caps_free(p_GUITaskState->DisplayBuffer2);
        p_GUITaskState->DisplayBuffer2 = NULL;
    }

    if (p_GUITaskState->Touch_IO_Handle != NULL) {
        esp_lcd_panel_io_del(p_GUITaskState->Touch_IO_Handle);
        p_GUITaskState->Touch_IO_Handle = NULL;
    }

    if (p_GUITaskState->PanelHandle != NULL) {
        esp_lcd_panel_del(p_GUITaskState->PanelHandle);
        p_GUITaskState->PanelHandle = NULL;
    }

    _lock_close(&p_GUITaskState->LVGL_API_Lock);
}

esp_err_t GUI_Helper_InitTouch(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Touch_Read_Callback)
{
    esp_err_t Error;

    if ((p_GUITaskState == NULL) || (p_GUITaskState->Touch_IO_Handle == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (p_GUITaskState->TouchHandle != NULL) {
        ESP_LOGW(TAG, "GT911 already initialized, skipping");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing GT911 touch controller (post-Lepton-boot)...");
    Error = esp_lcd_touch_new_i2c_gt911(p_GUITaskState->Touch_IO_Handle, &_GUI_Touch_Config,
                                        &p_GUITaskState->TouchHandle);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "GT911 touch controller initialization failed (0x%x)", Error);
        ESP_LOGW(TAG, "System will continue without touch functionality");

        p_GUITaskState->TouchHandle = NULL;
        p_GUITaskState->Touch = NULL;

        return ESP_OK;
    }

    ESP_LOGD(TAG, "GT911 initialized, registering LVGL indev...");
    p_GUITaskState->Touch = lv_indev_create();
    lv_indev_set_type(p_GUITaskState->Touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(p_GUITaskState->Touch, p_GUITaskState->Display);
    lv_indev_set_read_cb(p_GUITaskState->Touch, Touch_Read_Callback);
    lv_indev_set_user_data(p_GUITaskState->Touch, p_GUITaskState->TouchHandle);

    return ESP_OK;
}

esp_err_t GUI_Helper_InitKeypad(GUI_Task_State_t *p_GUITaskState, lv_indev_read_cb_t Keypad_Read_Callback)
{
    if (p_GUITaskState == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    p_GUITaskState->Keypad = lv_indev_create();
    lv_indev_set_type(p_GUITaskState->Keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(p_GUITaskState->Keypad, p_GUITaskState->Display);
    lv_indev_set_read_cb(p_GUITaskState->Keypad, Keypad_Read_Callback);

    ESP_LOGD(TAG, "Keypad indev registered");

    return ESP_OK;
}

void GUI_Helper_Timer_ClockUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    char Buffer[9];
    struct tm Now;

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Main) {
        return;
    }

    TimeManager_GetTime(&Now, NULL);

    snprintf(Buffer, sizeof(Buffer), "%02d:%02d:%02d", Now.tm_hour, Now.tm_min, Now.tm_sec);
    lv_label_set_text(ui_Label_Main_Time, Buffer);

    if (Server_IsRunning()) {
        WebSocket_BroadcastTelemetry();
    }
}

void GUI_Helper_Timer_SpotUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    App_GUI_Screenposition_t ScreenPosition;

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Main) {
        return;
    }

    int32_t ImageW = static_cast<int32_t>(lv_obj_get_width(ui_Image_Main_Image));
    int32_t ImageH = static_cast<int32_t>(lv_obj_get_height(ui_Image_Main_Image));

    if ((ImageW == 0) || (ImageH == 0)) {
        return;
    }

    /* The thermal canvas is rendered by LVGL with lv_image_set_rotation(1800).
     * LVGL does NOT apply this transform to child objects, so the crosshair
     * container stays at its logical (unrotated) position (LogicalCX, LogicalCY)
     * while the canvas content that is actually visible at that screen location is
     * stored at canvas pixel (ImageW-1-LogicalCX, ImageH-1-LogicalCY).
     *
     * We therefore flip both coordinates before sending the ScreenPosition to the
     * Lepton task, so Lepton_GetPixelTemperature reads the raw pixel that is truly
     * visible under the crosshair rather than the 180°-mirrored counterpart.
     *
     * The same flip is already implicitly used by the luminance overlay code in
     * guiTask.cpp, which maps canvas pixel (x,y) to visual position (W-1-x, H-1-y)
     * via XRot/YRot before comparing against label bounds. */
    int32_t LogicalCX = lv_obj_get_x_aligned(ui_Container_Main_Thermal_Crosshair) + lv_obj_get_width(
                            ui_Container_Main_Thermal_Crosshair) / 2;
    int32_t LogicalCY = lv_obj_get_y_aligned(ui_Container_Main_Thermal_Crosshair) + lv_obj_get_height(
                            ui_Container_Main_Thermal_Crosshair) / 2;

    ScreenPosition.x = (ImageW - 1) - LogicalCX;
    ScreenPosition.y = (ImageH - 1) - LogicalCY;
    ScreenPosition.Width = static_cast<int32_t>(ImageW);
    ScreenPosition.Height = static_cast<int32_t>(ImageH);

    ESP_LOGD(TAG, "Crosshair: logical (%d,%d) -> canvas (%d,%d), image size (%d,%d)",
             LogicalCX, LogicalCY,
             ScreenPosition.x, ScreenPosition.y,
             ScreenPosition.Width, ScreenPosition.Height);

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_PIXEL_TEMPERATURE, &ScreenPosition, sizeof(ScreenPosition),
                   pdMS_TO_TICKS(100));
}

void GUI_Helper_Timer_SceneStatisticsUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_SCENE_STATISTICS, NULL, 0, pdMS_TO_TICKS(100));
}

void GUI_Helper_Timer_LeptonUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_FPA_AUX_TEMP, NULL, 0, pdMS_TO_TICKS(100));
    esp_event_post(GUI_TASK_EVENTS, GUI_TASK_EVENT_REQUEST_UPTIME, NULL, 0, pdMS_TO_TICKS(100));
}

void GUI_Helper_Timer_RAMUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    char Buffer[32];

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Info) {
        return;
    }

    snprintf(Buffer, sizeof(Buffer), "%u KB / %u KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024);
    lv_label_set_text(ui_Label_Info_PSRAM_Free, Buffer);
    snprintf(Buffer, sizeof(Buffer), "%u KB / %u KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024);
    lv_label_set_text(ui_Label_Info_RAM_Free, Buffer);
}

void GUI_Helper_Timer_MemoryUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Info) {
        return;
    }

    ui_settings_update_memory_usage();
}

void GUI_Helper_Timer_UptimeUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    char Buffer[32];
    uint64_t UptimeSec = esp_timer_get_time() / 1000000;

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Info) {
        return;
    }

    snprintf(Buffer, sizeof(Buffer), "%02llu:%02llu:%02llu", UptimeSec / 3600, (UptimeSec % 3600) / 60,
             UptimeSec % 60);
    lv_label_set_text(ui_Label_Info_Uptime, Buffer);
}

void GUI_Helper_Timer_DistanceUpdate(lv_timer_t *p_Timer)
{
    uint16_t Distance;
    bool Valid;
    char Buffer[32];
    (void)p_Timer;

    if (lv_display_get_screen_active(lv_display_get_default()) != ui_Main) {
        return;
    }

    DevicesManager_GetDistance(&Distance, &Valid);

    snprintf(Buffer, sizeof(Buffer), "%s%.2f cm", Valid ? "" : "Invalid: ", static_cast<float>(Distance) / 10.0);

    lv_label_set_text(ui_Label_Main_Distance, Buffer);
}