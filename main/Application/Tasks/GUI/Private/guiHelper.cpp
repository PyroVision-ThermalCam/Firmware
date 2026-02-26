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
#include <driver/i2c.h>

#include "guiHelper.h"
#include "Application/application.h"
#include "../Export/ui.h"
#include "../UI/ui_settings.h"

#if defined(CONFIG_LCD_SPI2_HOST)
#define LCD_SPI_HOST                        SPI2_HOST
#elif defined(CONFIG_LCD_SPI3_HOST)
#define LCD_SPI_HOST                        SPI3_HOST
#else
#error "No SPI host defined for LCD!"
#endif

/* Partial render buffer: 1/5 of the screen (2 * 320 * 24 * 2 = 30720 bytes) in PSRAM.
 *
 * IMPORTANT: PSRAM is NOT in the ESP32-S3 DMA-capable address range (0x3FC88000-0x3FD00000),
 * so the SPI driver allocates an internal DMA bounce buffer for EACH queued transaction
 * (chunk_size = CONFIG_SPI_TRANSFER_SIZE = 4096 bytes each). The trans_queue_depth MUST be
 * kept low (currently 3) to limit simultaneous bounce buffer allocations (3 * 4096 = 12 KB),
 * because internal DMA RAM is scarce (~28 KB free after USB init). A higher queue depth
 * (e.g. 10 = 8 chunks * 4096 = 32 KB) would exceed free internal DMA RAM and cause
 * ESP_ERR_NO_MEM in spi_device_queue_trans, freezing the display.
 *
 * With ISR-based flush_ready (on_lcd_color_trans_done), LVGL correctly sequences
 * partial flushes without tearing. */
#define GUI_DRAW_BUFFER_SIZE                (2 * CONFIG_GUI_WIDTH * CONFIG_GUI_HEIGHT * sizeof(uint16_t) / 10)

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
    .scl_speed_hz = 400000,
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
inline void GUI_LVGL_TickTimer_CB(void *p_Arg)
{
    lv_tick_inc(CONFIG_GUI_LVGL_TICK_PERIOD_MS);
}

/** @brief          Initialize the GUI helper functions.
 *  @param p_Disp   Display handle
 *  @param p_Area   Area to update
 *  @param p_PxMap  Pixel data to flush
 */
static void GUI_LCD_Flush_CB(lv_display_t *p_Disp, const lv_area_t *p_Area, uint8_t *p_PxMap)
{
    int offsetx1 = p_Area->x1;
    int offsetx2 = p_Area->x2;
    int offsety1 = p_Area->y1;
    int offsety2 = p_Area->y2;

    esp_lcd_panel_draw_bitmap(static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(p_Disp)), offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, p_PxMap);
}

esp_err_t GUI_Helper_Init(GUI_Task_State_t *p_GUI_Task_State, lv_indev_read_cb_t Touch_Read_Callback)
{
    esp_err_t Error;
    uint32_t Caps;

#if (CONFIG_LCD_BL != -1)
    ESP_LOGD(TAG, "Configure LCD backlight GPIO...");
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << CONFIG_LCD_BL,
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_LCD_BL), LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGD(TAG, "Create I2C bus for touch controller...");

    p_GUI_Task_State->Touch_Bus_Handle = DevicesManager_GetTouchI2CBusHandle();

    ESP_LOGD(TAG, "Create panel IO...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_SPI_HOST), &_GUI_Panel_IO_Config,
                                             &p_GUI_Task_State->Panel_IO_Handle));
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(p_GUI_Task_State->Touch_Bus_Handle, &_GUI_Touch_IO_Config,
                                             &p_GUI_Task_State->Touch_IO_Handle));

    ESP_LOGD(TAG, "Initialize LVGL library and display...");
    lv_init();
    p_GUI_Task_State->Display = lv_display_create(CONFIG_GUI_WIDTH, CONFIG_GUI_HEIGHT);

    ESP_LOGD(TAG, "Register LCD panel IO callbacks...");
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(p_GUI_Task_State->Panel_IO_Handle, &_GUI_Panel_Callbacks,
                                                              p_GUI_Task_State->Display));

    ESP_LOGD(TAG, "Install ILI9341 panel driver...");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(p_GUI_Task_State->Panel_IO_Handle, &_GUI_Panel_Config,
                                              &p_GUI_Task_State->PanelHandle));
    ESP_LOGD(TAG, " ILI9341 panel driver installed");

    ESP_LOGD(TAG, "Reset the panel...");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(p_GUI_Task_State->PanelHandle));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel reset complete");

    ESP_LOGD(TAG, "Initialize the panel...");
    ESP_ERROR_CHECK(esp_lcd_panel_init(p_GUI_Task_State->PanelHandle));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel initialized");

    ESP_LOGD(TAG, "Configure panel for landscape mode...");
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(p_GUI_Task_State->PanelHandle, true));
    ESP_LOGD(TAG, " Panel swap_xy enabled for landscape");

    ESP_LOGD(TAG, "Configure panel mirroring...");
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(p_GUI_Task_State->PanelHandle, true, true));
    ESP_LOGD(TAG, " Panel mirroring configured (180 degree rotation)");

    ESP_LOGD(TAG, "Turn ON the panel display...");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(p_GUI_Task_State->PanelHandle, true));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGD(TAG, " Panel display turned ON");

    lv_display_set_flush_cb(p_GUI_Task_State->Display, GUI_LCD_Flush_CB);
    lv_display_set_user_data(p_GUI_Task_State->Display, p_GUI_Task_State->PanelHandle);

    #ifdef CONFIG_SPIRAM
        Caps = MALLOC_CAP_SPIRAM;
    #else
        Caps = 0;
    #endif

    p_GUI_Task_State->DisplayBuffer1 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, Caps);
    p_GUI_Task_State->DisplayBuffer2 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, Caps);
    ESP_LOGD(TAG, "Allocated LVGL buffers: %d bytes each in PSRAM", GUI_DRAW_BUFFER_SIZE);
    if ((p_GUI_Task_State->DisplayBuffer1 == NULL) || (p_GUI_Task_State->DisplayBuffer2 == NULL)) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers!");

        return ESP_ERR_NO_MEM;
    }

    lv_display_set_buffers(p_GUI_Task_State->Display,
                           p_GUI_Task_State->DisplayBuffer1,
                           p_GUI_Task_State->DisplayBuffer2,
                           GUI_DRAW_BUFFER_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGD(TAG, "Initialize GT911 touch controller...");
    Error = esp_lcd_touch_new_i2c_gt911(p_GUI_Task_State->Touch_IO_Handle, &_GUI_Touch_Config,
                                        &p_GUI_Task_State->TouchHandle);
    if (Error != ESP_OK) {
        ESP_LOGW(TAG, "GT911 touch controller initialization failed (0x%x)", Error);
        ESP_LOGW(TAG, "System will continue without touch functionality");

        p_GUI_Task_State->TouchHandle = NULL;
        p_GUI_Task_State->Touch = NULL;
    } else {
        ESP_LOGD(TAG, "GT911 touch controller initialized successfully");

        /* Register touchpad input device */
        ESP_LOGD(TAG, "Register touch input device to LVGL");
        p_GUI_Task_State->Touch = lv_indev_create();
        lv_indev_set_type(p_GUI_Task_State->Touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(p_GUI_Task_State->Touch, p_GUI_Task_State->Display);
        lv_indev_set_read_cb(p_GUI_Task_State->Touch, Touch_Read_Callback);
        lv_indev_set_user_data(p_GUI_Task_State->Touch, p_GUI_Task_State->TouchHandle);
    }

    const esp_timer_create_args_t LVGL_TickTimer_args = {
        .callback = &GUI_LVGL_TickTimer_CB,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };

    p_GUI_Task_State->EventGroup = xEventGroupCreate();
    if (p_GUI_Task_State->EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create GUI event group!");

        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_timer_create(&LVGL_TickTimer_args, &p_GUI_Task_State->LVGL_TickTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(p_GUI_Task_State->LVGL_TickTimer, CONFIG_GUI_LVGL_TICK_PERIOD_MS * 1000));

    p_GUI_Task_State->UpdateTimer[0] = lv_timer_create(GUI_Helper_Timer_ClockUpdate, 100, NULL);
    p_GUI_Task_State->UpdateTimer[1] = lv_timer_create(GUI_Helper_Timer_SpotUpdate, 2000, NULL);
    p_GUI_Task_State->UpdateTimer[2] = lv_timer_create(GUI_Helper_Timer_SpotmeterUpdate, 5000, NULL);
    p_GUI_Task_State->UpdateTimer[3] = lv_timer_create(GUI_Helper_Timer_SceneStatisticsUpdate, 5000, NULL);
    p_GUI_Task_State->UpdateTimer[4] = lv_timer_create(GUI_Helper_Timer_RAMUpdate, 5000, NULL);
    p_GUI_Task_State->UpdateTimer[5] = lv_timer_create(GUI_Helper_Timer_MemoryUpdate, 3000, NULL);

    _lock_init(&p_GUI_Task_State->LVGL_API_Lock);

    return ESP_OK;
}

void GUI_Helper_Deinit(GUI_Task_State_t *p_GUI_Task_State)
{
    if (p_GUI_Task_State->isInitialized == false) {
        return;
    }

    for (uint32_t i = 0; i < sizeof(p_GUI_Task_State->UpdateTimer) / sizeof(p_GUI_Task_State->UpdateTimer[0]); i++) {
        if (p_GUI_Task_State->UpdateTimer[i] != NULL) {
            lv_timer_delete(p_GUI_Task_State->UpdateTimer[i]);
            p_GUI_Task_State->UpdateTimer[i] = NULL;
        }
    }

    if (p_GUI_Task_State->LVGL_TickTimer != NULL) {
        esp_timer_stop(p_GUI_Task_State->LVGL_TickTimer);
        esp_timer_delete(p_GUI_Task_State->LVGL_TickTimer);
        p_GUI_Task_State->LVGL_TickTimer = NULL;
    }

    if (p_GUI_Task_State->EventGroup != NULL) {
        vEventGroupDelete(p_GUI_Task_State->EventGroup);
        p_GUI_Task_State->EventGroup = NULL;
    }

    if (p_GUI_Task_State->Touch != NULL) {
        lv_indev_delete(p_GUI_Task_State->Touch);
        p_GUI_Task_State->Touch = NULL;
    }

    esp_lcd_touch_del(p_GUI_Task_State->TouchHandle);

    if (p_GUI_Task_State->Display != NULL) {
        lv_display_delete(p_GUI_Task_State->Display);
        p_GUI_Task_State->Display = NULL;
    }

    if (p_GUI_Task_State->DisplayBuffer1 != NULL) {
        heap_caps_free(p_GUI_Task_State->DisplayBuffer1);
        p_GUI_Task_State->DisplayBuffer1 = NULL;
    }

    if (p_GUI_Task_State->DisplayBuffer2 != NULL) {
        heap_caps_free(p_GUI_Task_State->DisplayBuffer2);
        p_GUI_Task_State->DisplayBuffer2 = NULL;
    }

    if (p_GUI_Task_State->Touch_IO_Handle != NULL) {
        esp_lcd_panel_io_del(p_GUI_Task_State->Touch_IO_Handle);
        p_GUI_Task_State->Touch_IO_Handle = NULL;
    }

    if (p_GUI_Task_State->PanelHandle != NULL) {
        esp_lcd_panel_del(p_GUI_Task_State->PanelHandle);
        p_GUI_Task_State->PanelHandle = NULL;
    }

    _lock_close(&p_GUI_Task_State->LVGL_API_Lock);
}

void GUI_Helper_Timer_ClockUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    char Buffer[9];
    struct tm Now;

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

    if ((lv_obj_get_width(ui_Image_Thermal) == 0) || (lv_obj_get_height(ui_Image_Thermal) == 0)) {
        return;
    }

    /* Get crosshair position relative to its parent (ui_Image_Thermal) */
    ScreenPosition.x = lv_obj_get_x(ui_Label_Main_Thermal_Crosshair) + lv_obj_get_width(
                           ui_Label_Main_Thermal_Crosshair) / 2;
    ScreenPosition.y = lv_obj_get_y(ui_Label_Main_Thermal_Crosshair) + lv_obj_get_height(
                           ui_Label_Main_Thermal_Crosshair) / 2;
    ScreenPosition.Width = lv_obj_get_width(ui_Image_Thermal);
    ScreenPosition.Height = lv_obj_get_height(ui_Image_Thermal);

    ESP_LOGD(TAG, "Crosshair center in thermal canvas: (%d,%d), size (%d,%d)", ScreenPosition.x, ScreenPosition.y,
             ScreenPosition.Width, ScreenPosition.Height);

    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_PIXEL_TEMPERATURE, &ScreenPosition, sizeof(ScreenPosition), 0);
}

void GUI_Helper_Timer_SpotmeterUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_SPOTMETER, NULL, 0, 0);
}

void GUI_Helper_Timer_SceneStatisticsUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_SCENE_STATISTICS, NULL, 0, 0);
}

void GUI_Helper_Timer_RAMUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;
    char Buffer[16];

    sprintf(Buffer, "%u KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    lv_label_set_text(ui_Label_Info_PSRAM_Free, Buffer);
    sprintf(Buffer, "%u KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    lv_label_set_text(ui_Label_Info_RAM_Free, Buffer);
}

void GUI_Helper_Timer_MemoryUpdate(lv_timer_t *p_Timer)
{
    (void)p_Timer;

    ui_settings_update_memory_usage();
}