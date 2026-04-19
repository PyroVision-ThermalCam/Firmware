/*
 * devicesManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Devices Manager implementation.
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

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string.h>

#include "SPI/spi.h"
#include "TMP117/tmp117.h"
#include "RV8263C8/rv8263c8.h"
#include "PCAL6416AHF/pcal6416ahf.h"
#include "PCA9633DP1/pca9633dp1.h"
#include "MAX17048/max17048.h"
#include "VL53L1X/vl53l1x.h"
#include <driver/gpio.h>
#include <esp_task_wdt.h>

#include "devicesManager.h"
#include "Settings/settingsManager.h"
#include "../AppDiag/appDiag.h"

#define ADDR_PCAL6416AHF_MAINBOARD          0x20
#define ADDR_PCAL6416AHF_DISPLAYBOARD       0x21

#if defined(CONFIG_TOUCH_I2C0_HOST)
#define TOUCH_I2C_HOST                      I2C_NUM_0
#elif defined(CONFIG_TOUCH_I2C1_HOST)
#define TOUCH_I2C_HOST                      I2C_NUM_1
#else
#error "No I2C host defined for touch!"
#endif

#if defined(CONFIG_DEVICES_I2C_I2C0_HOST)
#define DEVICES_I2C_HOST                    I2C_NUM_0
#elif defined(CONFIG_DEVICES_I2C_I2C1_HOST)
#define DEVICES_I2C_HOST                    I2C_NUM_1
#else
#error "No I2C host defined for devices!"
#endif

ESP_EVENT_DEFINE_BASE(DEVICES_EVENTS);

/** @brief Default configuration for the I2C interface (shared by RTC, Port Expander, Lepton and Temperature Sensor).
 */
static i2c_master_bus_config_t _Devices_Manager_Devices_I2CM_Config = {
    .i2c_port = DEVICES_I2C_HOST,
    .sda_io_num = static_cast<gpio_num_t>(CONFIG_DEVICES_I2C_SDA),
    .scl_io_num = static_cast<gpio_num_t>(CONFIG_DEVICES_I2C_SCL),
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 0,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = false,
        .allow_pd = false,
    },
};

#if (CONFIG_DEVICES_I2C_SDA != CONFIG_TOUCH_SDA) || (CONFIG_DEVICES_I2C_SCL != CONFIG_TOUCH_SCL)
/** @brief Default configuration for the I2C interface (shared by Touch).
 */
static i2c_master_bus_config_t _Devices_Manager_Touch_I2CM_Config = {
    .i2c_port = TOUCH_I2C_HOST,
    .sda_io_num = static_cast<gpio_num_t>(CONFIG_TOUCH_SDA),
    .scl_io_num = static_cast<gpio_num_t>(CONFIG_TOUCH_SCL),
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .intr_priority = 0,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = true,
        .allow_pd = false,
    },
};
#endif

/** @brief SPI3 host device handle used by the Devices Manager (shared by LCD, touch, SD card). */
static const spi_host_device_t _Devices_Manager_Periph_SPI = SPI3_HOST;

/** @brief Default configuration for the SPI3 bus (shared by LCD, Touch, SD card).
 */
static const spi_bus_config_t _Devices_Manager_Periph_SPI_Config = {
    .mosi_io_num = CONFIG_SPI_MOSI,
    .miso_io_num = CONFIG_SPI_MISO,
    .sclk_io_num = CONFIG_SPI_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .data4_io_num = -1,
    .data5_io_num = -1,
    .data6_io_num = -1,
    .data7_io_num = -1,
    .data_io_default_level = 0,
    .max_transfer_sz = CONFIG_SPI_TRANSFER_SIZE,
    .flags = SPICOMMON_BUSFLAG_MASTER,
    .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
    .intr_flags = 0,
};

/** @brief TMP117 default settings:
 *      - Continuous mode
 *      - 1 second cycle
 *      - No averaging
 */
static const TMP117_Config_t _TMP117_DefaultConfig = {
    .Mode = TMP117_MODE_CONTINUOUS,
    .Cycle = TMP117_CYCLE_1S,
    .Averaging = TMP117_AVG_NONE,
};

/** @brief VL53L1X default settings:
 *      - Short distance mode
 *      - 100 ms timing budget
 *      - 100 ms inter-measurement period (back-to-back, continuous ranging)
 */
static const VL53L1X_Config_t _VL53L1X_DefaultConfig = {
    .DistanceMode = VL53L1X_DISTANCE_SHORT,
    .TimingBudget = VL53L1X_TIMING_100MS,
    .InterMeasurementMs = 100,
};

/** @brief Configuration for the mainboard expander INT# pin (plain input with pull-up for level polling). */
static const gpio_config_t _Devices_Manager_MainboardExpander_IntConf = {
    .pin_bit_mask = (1ULL << CONFIG_EXPANDER_INT_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

/** @brief Configuration for the displayboard expander INT# pin (plain input with pull-up for level polling). */
static const gpio_config_t _Devices_Manager_DisplayboardExpander_IntConf = {
    .pin_bit_mask = (1ULL << CONFIG_DISPLAYBOARD_EXPANDER_INT_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

/** @brief Pin configuration for the mainboard PCAL6416AHF (address 0x20).
 *         Register layout summary (Port 0 / Port 1, bit positions 0-7):
 *
 *         Port 0: [7]=TempInt [5]=RTCInt [3]=LeptonPwr [1]=BattChg [0]=BattAlert
 *         Port 1: [7]=CamPwr  [6]=CamRst [5]=LepRst    [4]=SDDetect [0]=RangeInt
 *
 *         Active-low outputs (LepRst, CamRst) are not hardware-inverted;
 *         the caller must negate the LOGDc value when calling PCAL6416AHF_WritePin.
 */
static const PCAL6416_IO_Conf_t _PCAL6416AHF_Mainboard_PinConfig[] = {
    /* Battery alert:  active low, pull-up; HW polarity inversion                               */
    { .Port = PCAL6416_PORT_0, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = true,  .IsLatched = false },
    /* Battery charge: active low                                                              */
    { .Port = PCAL6416_PORT_0, .Pin = 1, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = true,  .IsLatched = false },
    /* Lepton power: active high output                                                         */
    { .Port = PCAL6416_PORT_0, .Pin = 3, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* RTC interrupt:  active low, pull-up                                                      */
    { .Port = PCAL6416_PORT_0, .Pin = 5, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = false, .IsLatched = false },
    /* Temp interrupt: active low, pull-up                                                      */
    { .Port = PCAL6416_PORT_0, .Pin = 7, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = false, .IsLatched = false },
    /* Range interrupt: active low, pull-up                                                     */
    { .Port = PCAL6416_PORT_1, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = false, .IsLatched = false },
    /* SD card detect: active low, pull-up, latched (debounce)                                  */
    { .Port = PCAL6416_PORT_1, .Pin = 4, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .IsInverted = false, .IsLatched = true  },
    /* Lepton reset: active low output (caller negates)                                         */
    { .Port = PCAL6416_PORT_1, .Pin = 5, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* Camera reset: active low output (caller negates)                                         */
    { .Port = PCAL6416_PORT_1, .Pin = 6, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* Camera power: active high output                                                         */
    { .Port = PCAL6416_PORT_1, .Pin = 7, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
};

/** @brief Pin configuration for the displayboard PCAL6416AHF (address 0x21).
 *         Register layout summary:
 *
 *         Port 0: [7]=JoyCenter [6]=JoyRight [5]=JoyLeft [4]=JoyUp [3]=JoyDown
 *                 [2]=LED_Blue  [1]=LED_Green [0]=LED_Red
 *         Port 1: [3]=Btn4 [2]=Btn3 [1]=Btn2 [0]=Btn1
 *
 *         LEDs are active low outputs; buttons and joystick are active high with pull-down.
 */
static const PCAL6416_IO_Conf_t _PCAL6416AHF_Displayboard_PinConfig[] = {
    /* LED blue: active low output (caller negates)                                              */
    { .Port = PCAL6416_PORT_0, .Pin = 0, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* LED green: active low output (caller negates)                                            */
    { .Port = PCAL6416_PORT_0, .Pin = 1, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* LED red: active low output (caller negates)                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 2, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .IsInverted = false, .IsLatched = false },
    /* Joystick up: active high, pull-down input                                                */
    { .Port = PCAL6416_PORT_0, .Pin = 3, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Joystick center: active high, pull-down input                                              */
    { .Port = PCAL6416_PORT_0, .Pin = 4, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Joystick left: active high, pull-down input                                              */
    { .Port = PCAL6416_PORT_0, .Pin = 5, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Joystick right: active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 6, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Joystick down: active high, pull-down input                                            */
    { .Port = PCAL6416_PORT_0, .Pin = 7, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Button 1: active high, pull-down input                                                   */
    { .Port = PCAL6416_PORT_1, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Button 2: active high, pull-down input                                                   */
    { .Port = PCAL6416_PORT_1, .Pin = 1, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Button 3: active high, pull-down input                                                   */
    { .Port = PCAL6416_PORT_1, .Pin = 2, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
    /* Button 4: active high, pull-down input                                                   */
    { .Port = PCAL6416_PORT_1, .Pin = 3, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .IsInverted = false, .IsLatched = false },
};

typedef struct {
    bool IsInitialized;
    bool IsFlashEnabled;
    RV8263C8_Dev_t RTC;
    TMP117_Dev_t TMP117;
    PCAL6416AHF_Dev_t ExpanderMainboard;
    PCAL6416AHF_Dev_t ExpanderDisplayboard;
    MAX17048_Dev_t MAX17048;
    VL53L1X_Dev_t VL53L1X;
    i2c_master_bus_handle_t I2C_Bus_Handle;
    i2c_master_bus_handle_t Touch_I2C_Bus_Handle;
    PCA9633DP1_Dev_t PCA9633DP1;
    SemaphoreHandle_t Mutex;
} Devices_Manager_State_t;

static Devices_Manager_State_t _DevicesManagerState;

static const char *TAG = "Devices-Manager";

esp_err_t DevicesManager_Init(void)
{
    uint8_t Brightness;
    Settings_Display_t DisplaySettings;

    if (_DevicesManagerState.IsInitialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Devices Manager...");

    memset(&_DevicesManagerState, 0, sizeof(Devices_Manager_State_t));

    _DevicesManagerState.Mutex = xSemaphoreCreateRecursiveMutex();
    if (_DevicesManagerState.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C bus mutex!");

        return ESP_ERR_NO_MEM;
    }

#if (CONFIG_DEVICES_I2C_SDA != CONFIG_TOUCH_SDA) || (CONFIG_DEVICES_I2C_SCL != CONFIG_TOUCH_SCL)
    if (I2CM_Init(&_Devices_Manager_Touch_I2CM_Config, &_DevicesManagerState.Touch_I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Touch I2C!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_TOUCH_BUS_INIT);

        return DEVICES_ERR_I2C_TOUCH_BUS_INIT;
    }
#endif

#if (CONFIG_TOUCH_RST != -1)
    static const gpio_config_t Touch_RST_IO_Conf = {
        .pin_bit_mask = (1ULL << CONFIG_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&Touch_RST_IO_Conf));
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_TOUCH_RST), true);
#endif

    if (I2CM_Init(&_Devices_Manager_Devices_I2CM_Config, &_DevicesManagerState.I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Peripheral I2C!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_BUS_INIT);

        return DEVICES_ERR_I2C_BUS_INIT;
    }

    if (SPIM_Init(&_Devices_Manager_Periph_SPI_Config, _Devices_Manager_Periph_SPI, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI%u!", _Devices_Manager_Periph_SPI);

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_SPI_BUS_INIT);

        return DEVICES_ERR_SPI_BUS_INIT;
    }

    if (PCAL6416AHF_Init(&_DevicesManagerState.I2C_Bus_Handle,
                         ADDR_PCAL6416AHF_MAINBOARD,
                         _PCAL6416AHF_Mainboard_PinConfig,
                         sizeof(_PCAL6416AHF_Mainboard_PinConfig) / sizeof(_PCAL6416AHF_Mainboard_PinConfig[0]),
                         &_DevicesManagerState.ExpanderMainboard) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mainboard port expander!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_EXPANDER_MAINBOARD);

        return DEVICES_ERR_EXPANDER_MAINBOARD;
    }

    if (PCAL6416AHF_Init(&_DevicesManagerState.I2C_Bus_Handle,
                         ADDR_PCAL6416AHF_DISPLAYBOARD,
                         _PCAL6416AHF_Displayboard_PinConfig,
                         sizeof(_PCAL6416AHF_Displayboard_PinConfig) / sizeof(_PCAL6416AHF_Displayboard_PinConfig[0]),
                         &_DevicesManagerState.ExpanderDisplayboard) != ESP_OK) {
        ESP_LOGW(TAG, "Displayboard port expander not found - running without displayboard");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_EXPANDER_DISPLAYBOARD);

        return DEVICES_ERR_EXPANDER_DISPLAYBOARD;
    }

    /* OV5640 power-on sequence */
    /* Camera reset asserted (active-low: LOW = reset active) */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 6, false);

    /* Camera power ON */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 7, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Camera reset deasserted (HIGH = running) */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 6, true);

    /* Lepton power-on sequence per FLIR IDD:
     *   - Assert RESET_L (LOW) before or with power
     *   - Apply VDD
     *   - Wait tPWR (>= 5 ms)
     *   - Deassert RESET_L (HIGH)
     *   - Wait tBOOT (~5 s) before accessing CCI/I2C
     */
    /* Lepton reset asserted (active-low: LOW = reset active) */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 5, false);

    /* Lepton power ON */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 3, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Lepton reset deasserted (HIGH = running) */
    PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 5, true);

    I2CM_Scan(_DevicesManagerState.I2C_Bus_Handle);

#if (CONFIG_TOUCH_RST != -1)
    /* Hold GT911 touch controller in reset from the very start to prevent it from
     * interfering with the I2C bus during device initialization.
     * The GT911 will be properly released and initialized in GUI_Helper_Init.
     */
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_TOUCH_RST), false);
#endif

    if (RV8263C8_Init(&_DevicesManagerState.I2C_Bus_Handle, &_DevicesManagerState.RTC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RV8263C8!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_RV8263C8_NOT_FOUND);

        return DEVICES_ERR_RV8263C8_NOT_FOUND;
    }

    if (TMP117_Init(&_DevicesManagerState.I2C_Bus_Handle, &_DevicesManagerState.TMP117,
                    &_TMP117_DefaultConfig) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TMP117!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_TMP117_NOT_FOUND);

        return DEVICES_ERR_TMP117_NOT_FOUND;
    }

    if (MAX17048_Init(&_DevicesManagerState.I2C_Bus_Handle, &_DevicesManagerState.MAX17048) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MAX17048!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_MAX17048_NOT_FOUND);

        return DEVICES_ERR_MAX17048_NOT_FOUND;
    }

    if ((VL53L1X_Init(&_DevicesManagerState.I2C_Bus_Handle, &_DevicesManagerState.VL53L1X,
                      &_VL53L1X_DefaultConfig) != ESP_OK) || (VL53L1X_StartContinuous(&_DevicesManagerState.VL53L1X,
                                                                                      _VL53L1X_DefaultConfig.InterMeasurementMs) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to initialize VL53L1X!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_VL53L1X_NOT_FOUND);

        return DEVICES_ERR_VL53L1X_NOT_FOUND;
    }

    if (PCA9633DP1_Init(&_DevicesManagerState.I2C_Bus_Handle, &_DevicesManagerState.PCA9633DP1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCA9633DP1!");

        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_PCA9633_NOT_FOUND);

        return DEVICES_ERR_PCA9633_NOT_FOUND;
    }

    gpio_config(&_Devices_Manager_MainboardExpander_IntConf);
    gpio_config(&_Devices_Manager_DisplayboardExpander_IntConf);

    _DevicesManagerState.IsInitialized = true;

    SettingsManager_GetDisplay(&DisplaySettings);
    Brightness = static_cast<uint8_t>(static_cast<float>(DisplaySettings.Brightness) * 2.55);
    if ((DevicesManager_SetBrightness(BACKLIGHT_FLASH, 0) != ESP_OK) ||
        (DevicesManager_SetBrightness(BACKLIGHT_DISPLAY, Brightness) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to initialize brightness!");

        return DEVICES_ERR_I2C_COMM;
    }

    if (DevicesManager_SetLED(false, false, false) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDs!");

        return DEVICES_ERR_I2C_COMM;
    }

    _DevicesManagerState.IsFlashEnabled = false;

    return ESP_OK;
}

esp_err_t DevicesManager_Deinit(void)
{
    if (_DevicesManagerState.IsInitialized == false) {
        return ESP_OK;
    }

    PCA9633DP1_Deinit(&_DevicesManagerState.PCA9633DP1);
    VL53L1X_Deinit(&_DevicesManagerState.VL53L1X);
    MAX17048_Deinit(&_DevicesManagerState.MAX17048);
    TMP117_Deinit(&_DevicesManagerState.TMP117);
    RV8263C8_Deinit(&_DevicesManagerState.RTC);
    PCAL6416AHF_Deinit(&_DevicesManagerState.ExpanderDisplayboard);
    PCAL6416AHF_Deinit(&_DevicesManagerState.ExpanderMainboard);

    I2CM_Deinit(_DevicesManagerState.I2C_Bus_Handle);
    SPIM_Deinit(_Devices_Manager_Periph_SPI);

    if (_DevicesManagerState.Mutex != NULL) {
        vSemaphoreDelete(_DevicesManagerState.Mutex);
        _DevicesManagerState.Mutex = NULL;
    }

    _DevicesManagerState.IsInitialized = false;

    return ESP_OK;
}

esp_err_t DevicesManager_AcquireI2CBus(TickType_t Timeout)
{
    if (_DevicesManagerState.IsInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTakeRecursive(_DevicesManagerState.Mutex, Timeout) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void DevicesManager_ReleaseI2CBus(void)
{
    xSemaphoreGiveRecursive(_DevicesManagerState.Mutex);
}

i2c_master_bus_handle_t DevicesManager_GetI2CBusHandle(void)
{
    if (_DevicesManagerState.IsInitialized == false) {
        return NULL;
    }

    return _DevicesManagerState.I2C_Bus_Handle;
}

i2c_master_bus_handle_t DevicesManager_GetTouchI2CBusHandle(void)
{
    if (_DevicesManagerState.IsInitialized == false) {
        return NULL;
    }

#if (CONFIG_DEVICES_I2C_SDA != CONFIG_TOUCH_SDA) || (CONFIG_DEVICES_I2C_SCL != CONFIG_TOUCH_SCL)
    return _DevicesManagerState.Touch_I2C_Bus_Handle;
#else
    return _DevicesManagerState.I2C_Bus_Handle;
#endif
}

spi_host_device_t DevicesManager_GetSPIHost(void)
{
    return _Devices_Manager_Periph_SPI;
}

esp_err_t DevicesManager_GetBatteryStatus(int *p_Voltage, uint8_t *p_Percentage, bool *p_Charging)
{
    float SOC;
    float Voltage;
    esp_err_t Error = ESP_OK;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if ((p_Voltage == NULL) || (p_Percentage == NULL) || (p_Charging == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if ((MAX17048_GetVoltage(&_DevicesManagerState.MAX17048, &Voltage) != ESP_OK) ||
        (MAX17048_GetSOC(&_DevicesManagerState.MAX17048, &SOC) != ESP_OK)) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_ADC_READ);

        Error = DEVICES_ERR_ADC_READ;
    } else if (PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 1, p_Charging) != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        Error = DEVICES_ERR_I2C_COMM;
    } else {
        *p_Percentage = static_cast<uint8_t>(SOC);
        *p_Voltage = static_cast<int>(Voltage * 1000.0f);

        ESP_LOGD(TAG, "Battery voltage: %.3f V, SOC: %.1f%%", Voltage, SOC);
    }

    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_GetRTCHandle(RV8263C8_Dev_t *p_Handle)
{
    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if (p_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *p_Handle = _DevicesManagerState.RTC;

    return ESP_OK;
}

esp_err_t DevicesManager_GetTime(struct tm *p_Time)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if (p_Time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = RV8263C8_GetTime(&_DevicesManagerState.RTC, p_Time);
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_SetTime(const struct tm *p_Time)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if (p_Time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = RV8263C8_SetTime(&_DevicesManagerState.RTC, p_Time);
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_GetTemperature(float *p_Temperature)
{
    esp_err_t Error = ESP_OK;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if (p_Temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if (TMP117_ReadTemperature(&_DevicesManagerState.TMP117, p_Temperature) != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        Error = DEVICES_ERR_I2C_COMM;
    } else {
        ESP_LOGD(TAG, "Current temperature: %.2f °C", *p_Temperature);
    }

    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_SetBrightness(Devices_BacklightID_t ID, uint8_t Brightness)
{
    esp_err_t Error = ESP_OK;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if (ID == BACKLIGHT_FLASH) {
        if (Brightness > 0) {
            if ((PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED0, PCA9633_LED_PWM) != ESP_OK) ||
                (PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED1, PCA9633_LED_PWM) != ESP_OK)) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            } else if ((PCA9633DP1_SetLEDBrightness(&_DevicesManagerState.PCA9633DP1, PCA9633_LED0, Brightness) != ESP_OK) ||
                       (PCA9633DP1_SetLEDBrightness(&_DevicesManagerState.PCA9633DP1, PCA9633_LED1, Brightness) != ESP_OK)) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            }
        } else {
            if ((PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED0, PCA9633_LED_OFF) != ESP_OK) ||
                (PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED1, PCA9633_LED_OFF) != ESP_OK)) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            }
        }
    } else if (ID == BACKLIGHT_DISPLAY) {
        if (Brightness > 0) {
            if (PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED2, PCA9633_LED_PWM) != ESP_OK) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            } else if (PCA9633DP1_SetLEDBrightness(&_DevicesManagerState.PCA9633DP1, PCA9633_LED2, Brightness) != ESP_OK) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            }
        } else {
            if (PCA9633DP1_SetLEDState(&_DevicesManagerState.PCA9633DP1, PCA9633_LED2, PCA9633_LED_OFF) != ESP_OK) {
                APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

                Error = DEVICES_ERR_I2C_COMM;
            }
        }
    } else {
        Error = ESP_ERR_INVALID_ARG;
    }

    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_LeptonReset(bool Reset)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard,
                                 PCAL6416_PORT_1, 5, (Reset == false));
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_SetLeptonPower(bool Enable)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 3, Enable);
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_HandleExpanderInterrupt(void)
{
    uint8_t Status0, Status1, In0, In1;
    bool Level;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    /* INT# is active-low open-drain: level high means no pin changed state */
    if (gpio_get_level(static_cast<gpio_num_t>(CONFIG_EXPANDER_INT_GPIO)) != 0) {
        return ESP_OK;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if (PCAL6416AHF_ReadIntStatus(&_DevicesManagerState.ExpanderMainboard, &Status0, &Status1, &In0, &In1) != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        DevicesManager_ReleaseI2CBus();

        return DEVICES_ERR_I2C_COMM;
    }

    DevicesManager_ReleaseI2CBus();

    if (Status0 & (1 << 0)) {
        Level = ((In0 & (1 << 0)) != 0);

        ESP_LOGD(TAG, "Battery alert state changed! Level: 0x%X", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_BATTERY_ALERT, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 1)) {
        Level = ((In0 & (1 << 1)) != 0);

        ESP_LOGD(TAG, "Battery charging state changed! Level: 0x%X", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_BATTERY_CHARGING, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 5)) {
        ESP_LOGD(TAG, "RTC interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_RTC_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 7)) {
        ESP_LOGD(TAG, "Temperature interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_TEMP_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status1 & (1 << 0)) {
        ESP_LOGD(TAG, "Range interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_RANGE_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status1 & (1 << 4)) {
        Level = ((In1 & (1 << 4)) == 0);

        ESP_LOGD(TAG, "SD card detect changed! Inserted: 0x%X", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

esp_err_t DevicesManager_HandleDisplayboardExpanderInterrupt(Devices_Input_State_t *p_State)
{
    uint8_t Status0, Status1, In0, In1;

    if (p_State == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    /* INT# is active-low open-drain: level high means no pin state change */
    if (gpio_get_level(static_cast<gpio_num_t>(CONFIG_DISPLAYBOARD_EXPANDER_INT_GPIO)) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Reading INT_STATUS0/1 clears all pending bits and releases INT#.
     * The subsequent Input Port read (done inside ReadIntStatus) returns the current state.
     * Use a bounded timeout so callers that run outside the shared I2C block are not
     * blocked indefinitely when the Lepton task holds the bus (portMAX_DELAY in a
     * dedicated 10 ms poll loop would defeat the purpose of interrupt-driven detection). */
    if (DevicesManager_AcquireI2CBus(pdMS_TO_TICKS(250)) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    if (PCAL6416AHF_ReadIntStatus(&_DevicesManagerState.ExpanderDisplayboard,
                                  &Status0, &Status1, &In0, &In1) != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        /* The I2C transaction failed, most likely because another device on the bus
         * (e.g. Lepton camera during a CCI operation) left SDA or SCL held LOW
         * (ESP_ERR_INVALID_STATE = 0x103). Recover by sending nine SCL clock pulses
         * so that any stuck device releases SDA before the next transaction. */
        i2c_master_bus_reset(_DevicesManagerState.I2C_Bus_Handle);
        vTaskDelay(pdMS_TO_TICKS(20));

        DevicesManager_ReleaseI2CBus();

        return DEVICES_ERR_I2C_COMM;
    }

    DevicesManager_ReleaseI2CBus();

    ESP_LOGD(TAG, "Displayboard INT: Status0=0x%02X Status1=0x%02X In0=0x%02X In1=0x%02X",
             Status0, Status1, In0, In1);

    p_State->JoyUp = ((In0 & (1 << 3)) != 0);
    p_State->JoyDown = ((In0 & (1 << 7)) != 0);
    p_State->JoyLeft = ((In0 & (1 << 5)) != 0);
    p_State->JoyRight = ((In0 & (1 << 6)) != 0);
    p_State->JoyCenter = ((In0 & (1 << 4)) != 0);
    p_State->Button4 = ((In1 & (1 << 0)) != 0);
    p_State->Button3 = ((In1 & (1 << 1)) != 0);
    p_State->Button2 = ((In1 & (1 << 2)) != 0);
    p_State->Button1 = ((In1 & (1 << 3)) != 0);

    return ESP_OK;
}

esp_err_t DevicesManager_SetCameraReset(bool Reset)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 6, (Reset == false));
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_SetCameraPower(bool Enable)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 7, Enable);
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_GetBatteryAlert(bool *p_Alert)
{
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 0, p_Alert);
    DevicesManager_ReleaseI2CBus();

    return Error;
}

esp_err_t DevicesManager_GetRTCInterrupt(bool *p_Triggered)
{
    bool Level;
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 5, &Level);
    DevicesManager_ReleaseI2CBus();

    if (Error != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        return DEVICES_ERR_I2C_COMM;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetTempInterrupt(bool *p_Triggered)
{
    bool Level;
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_0, 7, &Level);
    DevicesManager_ReleaseI2CBus();

    if (Error != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        return DEVICES_ERR_I2C_COMM;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetRangeInterrupt(bool *p_Triggered)
{
    bool Level;
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 0, &Level);
    DevicesManager_ReleaseI2CBus();

    if (Error != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        return DEVICES_ERR_I2C_COMM;
    }

    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetSDDetect(bool *p_Inserted)
{
    bool Level;
    esp_err_t Error;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);
    Error = PCAL6416AHF_ReadPin(&_DevicesManagerState.ExpanderMainboard, PCAL6416_PORT_1, 4, &Level);
    DevicesManager_ReleaseI2CBus();

    if (Error != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        return DEVICES_ERR_I2C_COMM;
    }

    *p_Inserted = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetDistance(uint16_t *p_Distance_mm, bool *p_IsValid)
{
    bool Ready = false;
    VL53L1X_Result_t Result;
    esp_err_t Error;

    if ((p_Distance_mm == NULL) || (p_IsValid == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    /* Sensor runs in continuous mode. Poll until the next measurement is ready
     * (inter-measurement period = 100 ms, timeout = 50 × 10 ms = 500 ms max).
     * VL53L1X_ClearInterrupt() re-arms the sensor for the next cycle after reading.
     * Acquire the I2C bus only for each I2C call so the bus is not held during vTaskDelay.
     */
    for (uint32_t Retry = 0; Retry < 50; Retry++) {
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(10));

        DevicesManager_AcquireI2CBus(portMAX_DELAY);
        Error = VL53L1X_IsDataReady(&_DevicesManagerState.VL53L1X, &Ready);
        DevicesManager_ReleaseI2CBus();

        if (Error != ESP_OK) {
            APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

            return DEVICES_ERR_I2C_COMM;
        }

        if (Ready) {
            break;
        }
    }

    if (Ready == false) {
        DevicesManager_AcquireI2CBus(portMAX_DELAY);
        Error = VL53L1X_ClearInterrupt(&_DevicesManagerState.VL53L1X);
        DevicesManager_ReleaseI2CBus();

        if (Error != ESP_OK) {
            APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

            return DEVICES_ERR_I2C_COMM;
        }

        return ESP_ERR_TIMEOUT;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if ((VL53L1X_GetResult(&_DevicesManagerState.VL53L1X, &Result) != ESP_OK) ||
        (VL53L1X_ClearInterrupt(&_DevicesManagerState.VL53L1X) != ESP_OK)) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        DevicesManager_ReleaseI2CBus();

        return DEVICES_ERR_I2C_COMM;
    }

    DevicesManager_ReleaseI2CBus();

    *p_Distance_mm = Result.Distance_mm;
    *p_IsValid = (Result.Status == VL53L1X_RANGE_VALID);

    ESP_LOGD(TAG, "Measured distance: %u mm, Status: %u, Signal rate: %u MCPS, Ambient rate: %u MCPS, SPADs: %u",
             Result.Distance_mm, Result.Status, Result.SignalRateMCPS, Result.AmbientRateMCPS, Result.EffectiveSPADs);

    return ESP_OK;
}

esp_err_t DevicesManager_GetDisplayboardInputs(Devices_Input_State_t *p_State)
{
    uint8_t In0;
    uint8_t In1;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    } else if (p_State == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    if (PCAL6416AHF_ReadInputs(&_DevicesManagerState.ExpanderDisplayboard, &In0, &In1) != ESP_OK) {
        APP_DIAG_RECORD(APP_DIAG_SOURCE_DEVICES, DEVICES_ERR_I2C_COMM);

        DevicesManager_ReleaseI2CBus();

        return DEVICES_ERR_I2C_COMM;
    }

    p_State->JoyUp = ((In0 & (1 << 3)) != 0);
    p_State->JoyDown = ((In0 & (1 << 4)) != 0);
    p_State->JoyLeft = ((In0 & (1 << 5)) != 0);
    p_State->JoyRight = ((In0 & (1 << 6)) != 0);
    p_State->JoyCenter = ((In0 & (1 << 7)) != 0);
    p_State->Button4 = ((In1 & (1 << 0)) != 0);
    p_State->Button3 = ((In1 & (1 << 1)) != 0);
    p_State->Button2 = ((In1 & (1 << 2)) != 0);
    p_State->Button1 = ((In1 & (1 << 3)) != 0);

    DevicesManager_ReleaseI2CBus();

    return ESP_OK;
}

esp_err_t DevicesManager_SetLED(bool R, bool G, bool B)
{
    esp_err_t Error = ESP_OK;

    if (_DevicesManagerState.IsInitialized == false) {
        return DEVICES_ERR_NOT_INITIALIZED;
    }

    DevicesManager_AcquireI2CBus(portMAX_DELAY);

    /* Port 0 bit layout: P0.0=LED_Red, P0.1=LED_Green, P0.2=LED_Blue (all active-low outputs) */
    Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderDisplayboard,
                                 PCAL6416_PORT_0, 0, (R == false));
    if (Error == ESP_OK) {
        Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderDisplayboard,
                                     PCAL6416_PORT_0, 1, (G == false));
    }

    if (Error == ESP_OK) {
        Error = PCAL6416AHF_WritePin(&_DevicesManagerState.ExpanderDisplayboard,
                                     PCAL6416_PORT_0, 2, (B == false));
    }

    DevicesManager_ReleaseI2CBus();

    return Error;
}

void DevicesManager_IsFlashEnabled(bool *p_Enabled)
{
    if (p_Enabled == NULL) {
        return;
    }

    *p_Enabled = _DevicesManagerState.IsFlashEnabled;
}

void DevicesManager_SetFlashEnable(bool Enable)
{
    Settings_Display_t DisplaySettings;

    _DevicesManagerState.IsFlashEnabled = Enable;

    if (Enable) {
        /* Flash ON: set flash brightness to the current level (0 = off, >0 = on) */
        SettingsManager_GetDisplay(&DisplaySettings);
        uint8_t Brightness = static_cast<uint8_t>(static_cast<float>(DisplaySettings.Brightness) * 2.55);
        DevicesManager_SetBrightness(BACKLIGHT_FLASH, Brightness);
    } else {
        /* Flash OFF: set flash brightness to 0 (off) */
        DevicesManager_SetBrightness(BACKLIGHT_FLASH, 0);
    }
}

void DevicesManager_ToggleFlashEnable(void)
{
    DevicesManager_SetFlashEnable(!_DevicesManagerState.IsFlashEnabled);
}