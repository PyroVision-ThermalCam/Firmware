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

/** @brief
 */
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

/** @brief Pin configuration for the mainboard PCAL6416AHF (address 0x20).
 *         Register layout summary (Port 0 / Port 1, bit positions 0-7):
 *
 *         Port 0: [7]=TempInt [5]=RTCInt [2]=LeptonPwr [1]=BattChg [0]=BattAlert
 *         Port 1: [7]=CamPwr  [6]=CamRst [5]=LepRst    [4]=SDDetect [0]=RangeInt
 *
 *         Active-low outputs (LepRst, CamRst) are not hardware-inverted;
 *         the caller must negate the logic value when calling PCAL6416AHF_WritePin.
 */
static const PCAL6416_IO_Conf_t _PCAL6416AHF_Mainboard_PinConfig[] = {
    /* Battery alert:  active low, pull-up; HW polarity inversion                               */
    { .Port = PCAL6416_PORT_0, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .isInverted = true,  .isLatched = false },
    /* Battery charge: active high                                                               */
    { .Port = PCAL6416_PORT_0, .Pin = 1, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* Lepton power:   active high output                                                        */
    { .Port = PCAL6416_PORT_0, .Pin = 2, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* RTC interrupt:  active low, pull-up                                                       */
    { .Port = PCAL6416_PORT_0, .Pin = 5, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .isInverted = false, .isLatched = false },
    /* Temp interrupt: active low, pull-up                                                       */
    { .Port = PCAL6416_PORT_0, .Pin = 7, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .isInverted = false, .isLatched = false },
    /* Range interrupt: active low, pull-up                                                      */
    { .Port = PCAL6416_PORT_1, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .isInverted = false, .isLatched = false },
    /* SD card detect: active low, pull-up, latched (debounce)                                   */
    { .Port = PCAL6416_PORT_1, .Pin = 4, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_UP,   .isInverted = false, .isLatched = true  },
    /* Lepton reset:   active low output (caller negates)                                        */
    { .Port = PCAL6416_PORT_1, .Pin = 5, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* Camera reset:   active low output (caller negates)                                        */
    { .Port = PCAL6416_PORT_1, .Pin = 6, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* Camera power:   active high output                                                        */
    { .Port = PCAL6416_PORT_1, .Pin = 7, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
};

/** @brief Pin configuration for the displayboard PCAL6416AHF (address 0x21).
 *         Register layout summary:
 *
 *         Port 0: [7]=JoyCenter [6]=JoyRight [5]=JoyLeft [4]=JoyDown [3]=JoyUp
 *                 [2]=LED_Red  [1]=LED_Green [0]=LED_Blue
 *         Port 1: [3]=Btn4 [2]=Btn3 [1]=Btn2 [0]=Btn1
 *
 *         LEDs are active low outputs; buttons and joystick are active high with pull-down.
 */
static const PCAL6416_IO_Conf_t _PCAL6416AHF_Displayboard_PinConfig[] = {
    /* LED red:     active low output; isInverted=false because POL register does not affect outputs;
     *              the caller negates the logic level (see DevicesManager_SetLED)               */
    { .Port = PCAL6416_PORT_0, .Pin = 0, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* LED green:   active low output (caller negates)                                           */
    { .Port = PCAL6416_PORT_0, .Pin = 1, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* LED blue:    active low output (caller negates)                                           */
    { .Port = PCAL6416_PORT_0, .Pin = 2, .Direction = PCAL6416_DIR_OUTPUT, .Pull = PCAL6416_PULL_NONE, .isInverted = false, .isLatched = false },
    /* Joystick up:     active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 3, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Joystick down:   active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 4, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Joystick left:   active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 5, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Joystick right:  active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 6, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Joystick center: active high, pull-down input                                             */
    { .Port = PCAL6416_PORT_0, .Pin = 7, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Button 1: active high, pull-down input                                                    */
    { .Port = PCAL6416_PORT_1, .Pin = 0, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Button 2: active high, pull-down input                                                    */
    { .Port = PCAL6416_PORT_1, .Pin = 1, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Button 3: active high, pull-down input                                                    */
    { .Port = PCAL6416_PORT_1, .Pin = 2, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
    /* Button 4: active high, pull-down input                                                    */
    { .Port = PCAL6416_PORT_1, .Pin = 3, .Direction = PCAL6416_DIR_INPUT,  .Pull = PCAL6416_PULL_DOWN, .isInverted = false, .isLatched = false },
};

typedef struct {
    bool initialized;
    RV8263C8_Dev_t RTC;
    TMP117_Dev_t TMP117;
    PCAL6416AHF_Dev_t Expander_Mainboard;
    PCAL6416AHF_Dev_t Expander_Displayboard;
    MAX17048_Dev_t MAX17048;
    VL53L1X_Dev_t VL53L1X;
    i2c_master_bus_handle_t I2C_Bus_Handle;
    i2c_master_bus_handle_t Touch_I2C_Bus_Handle;
    PCA9633DP1_Dev_t PCA9633DP1;
} Devices_Manager_State_t;

static Devices_Manager_State_t _Devices_Manager_State;

static const char *TAG = "Devices-Manager";

esp_err_t DevicesManager_Init(void)
{
    if (_Devices_Manager_State.initialized) {
        ESP_LOGW(TAG, "Already initialized");

        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Devices Manager...");

    memset(&_Devices_Manager_State, 0, sizeof(Devices_Manager_State_t));

#if (CONFIG_DEVICES_I2C_SDA != CONFIG_TOUCH_SDA) || (CONFIG_DEVICES_I2C_SCL != CONFIG_TOUCH_SCL)
    if (I2CM_Init(&_Devices_Manager_Touch_I2CM_Config, &_Devices_Manager_State.Touch_I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Touch I2C!");

        return ESP_FAIL;
    }
#endif

    if (I2CM_Init(&_Devices_Manager_Devices_I2CM_Config, &_Devices_Manager_State.I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Peripheral I2C!");

        return ESP_FAIL;
    }

    if (SPIM_Init(&_Devices_Manager_Periph_SPI_Config, _Devices_Manager_Periph_SPI, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI%u!", _Devices_Manager_Periph_SPI);

        return ESP_FAIL;
    }

    I2CM_Scan(_Devices_Manager_State.I2C_Bus_Handle);

    if (PCAL6416AHF_Init(&_Devices_Manager_State.I2C_Bus_Handle,
                         ADDR_PCAL6416AHF_MAINBOARD,
                         _PCAL6416AHF_Mainboard_PinConfig,
                         sizeof(_PCAL6416AHF_Mainboard_PinConfig) / sizeof(_PCAL6416AHF_Mainboard_PinConfig[0]),
                         &_Devices_Manager_State.Expander_Mainboard) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mainboard port expander!");

        return ESP_FAIL;
    }

    if (PCAL6416AHF_Init(&_Devices_Manager_State.I2C_Bus_Handle,
                         ADDR_PCAL6416AHF_DISPLAYBOARD,
                         _PCAL6416AHF_Displayboard_PinConfig,
                         sizeof(_PCAL6416AHF_Displayboard_PinConfig) / sizeof(_PCAL6416AHF_Displayboard_PinConfig[0]),
                         &_Devices_Manager_State.Expander_Displayboard) != ESP_OK) {
        ESP_LOGW(TAG, "Displayboard port expander not found - running without displayboard");
    }

    if (RV8263C8_Init(&_Devices_Manager_State.I2C_Bus_Handle, &_Devices_Manager_State.RTC) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RV8263C8!");

        return ESP_FAIL;
    }

    if (TMP117_Init(&_Devices_Manager_State.I2C_Bus_Handle, &_Devices_Manager_State.TMP117,
                    &_TMP117_DefaultConfig) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TMP117!");

        return ESP_FAIL;
    }

    if (MAX17048_Init(&_Devices_Manager_State.I2C_Bus_Handle, &_Devices_Manager_State.MAX17048) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MAX17048!");

        return ESP_FAIL;
    }

    if ((VL53L1X_Init(&_Devices_Manager_State.I2C_Bus_Handle, &_Devices_Manager_State.VL53L1X,
                      &_VL53L1X_DefaultConfig) != ESP_OK) || (VL53L1X_StartContinuous(&_Devices_Manager_State.VL53L1X,
                                                                                      _VL53L1X_DefaultConfig.InterMeasurementMs) != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to initialize VL53L1X!");

        return ESP_FAIL;
    }

    if (PCA9633DP1_Init(&_Devices_Manager_State.I2C_Bus_Handle, &_Devices_Manager_State.PCA9633DP1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PCA9633DP1!");

        return ESP_FAIL;
    }

    gpio_config(&_Devices_Manager_MainboardExpander_IntConf);

    _Devices_Manager_State.initialized = true;

    /* NOTE: Temporary test call – set all LED channels to 1 % duty cycle so that PWM
     * is visible on an oscilloscope. Remove or set to 0 before the production release. */
    DevicesManager_SetLEDBrightness(1);

    // TODO: Anders machen
    DevicesManager_LeptonReset(false);

    DevicesManager_SetLED(false, false, false);

    return ESP_OK;
}

esp_err_t DevicesManager_Deinit(void)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");

        return ESP_OK;
    }

    PCA9633DP1_Deinit(&_Devices_Manager_State.PCA9633DP1);
    VL53L1X_Deinit(&_Devices_Manager_State.VL53L1X);
    MAX17048_Deinit(&_Devices_Manager_State.MAX17048);
    TMP117_Deinit(&_Devices_Manager_State.TMP117);
    RV8263C8_Deinit(&_Devices_Manager_State.RTC);
    PCAL6416AHF_Deinit(&_Devices_Manager_State.Expander_Displayboard);
    PCAL6416AHF_Deinit(&_Devices_Manager_State.Expander_Mainboard);

    I2CM_Deinit(_Devices_Manager_State.I2C_Bus_Handle);
    SPIM_Deinit(_Devices_Manager_Periph_SPI);

    _Devices_Manager_State.initialized = false;

    return ESP_OK;
}

i2c_master_bus_handle_t DevicesManager_GetI2CBusHandle(void)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");

        return NULL;
    }

    return _Devices_Manager_State.I2C_Bus_Handle;
}

i2c_master_bus_handle_t DevicesManager_GetTouchI2CBusHandle(void)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");

        return NULL;
    }

#if (CONFIG_DEVICES_I2C_SDA != CONFIG_TOUCH_SDA) || (CONFIG_DEVICES_I2C_SCL != CONFIG_TOUCH_SCL)
    return _Devices_Manager_State.Touch_I2C_Bus_Handle;
#else
    return _Devices_Manager_State.I2C_Bus_Handle;
#endif
}

spi_host_device_t DevicesManager_GetSPIHost(void)
{
    return _Devices_Manager_Periph_SPI;
}

esp_err_t DevicesManager_GetBatteryVoltage(int *p_Voltage, uint8_t *p_Percentage)
{
    esp_err_t Error;
    float VoltageV;
    float SOC;

    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");

        return ESP_ERR_INVALID_STATE;
    } else if ((p_Voltage == NULL) || (p_Percentage == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    Error = MAX17048_GetVoltage(&_Devices_Manager_State.MAX17048, &VoltageV);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Convert V to mV */
    *p_Voltage = static_cast<int>(VoltageV * 1000.0f);

    Error = MAX17048_GetSOC(&_Devices_Manager_State.MAX17048, &SOC);
    if (Error != ESP_OK) {
        return Error;
    }

    *p_Percentage = static_cast<uint8_t>(SOC);

    return ESP_OK;
}

esp_err_t DevicesManager_GetRTCHandle(RV8263C8_Dev_t *p_Handle)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");

        return ESP_ERR_INVALID_STATE;
    } else if (p_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *p_Handle = _Devices_Manager_State.RTC;

    return ESP_OK;
}

esp_err_t DevicesManager_GetTime(struct tm *p_Time)
{
    return RV8263C8_GetTime(&_Devices_Manager_State.RTC, p_Time);
}

esp_err_t DevicesManager_SetTime(const struct tm *p_Time)
{
    return RV8263C8_SetTime(&_Devices_Manager_State.RTC, p_Time);
}

esp_err_t DevicesManager_GetTemperature(float *p_Temperature)
{
    return TMP117_ReadTemperature(&_Devices_Manager_State.TMP117, p_Temperature);
}

esp_err_t DevicesManager_SetLEDBrightness(uint8_t Brightness)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return PCA9633DP1_SetAllLEDs(&_Devices_Manager_State.PCA9633DP1,
                                 Brightness, Brightness, Brightness, Brightness);
}

esp_err_t DevicesManager_LeptonReset(bool Reset)
{
    /* Lepton reset is active low: assert reset when Reset=true → drive pin low */
    return PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Mainboard,
                                PCAL6416_PORT_1, 5, (Reset == false));
}

esp_err_t DevicesManager_HandleExpanderInterrupt(void)
{
    esp_err_t Error;

    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* INT# is active-low open-drain: level high means no pin changed state */
    if (gpio_get_level(static_cast<gpio_num_t>(CONFIG_EXPANDER_INT_GPIO)) != 0) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Expander: processing pin changes");

    uint8_t Status0, Status1, In0, In1;
    bool Level;

    Error = PCAL6416AHF_ReadIntStatus(&_Devices_Manager_State.Expander_Mainboard,
                                      &Status0, &Status1, &In0, &In1);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read expander interrupt status: 0x%x! Attempting I2C bus recovery.", Error);

        i2c_master_bus_reset(_Devices_Manager_State.I2C_Bus_Handle);

        return Error;
    }

    if (Status0 & (1 << 0)) {
        /* Battery alert: HW polarity-inverted (POL0.0 set) → bit=1 in In0 means alert active */
        Level = ((In0 & (1 << 0)) != 0);
        ESP_LOGI(TAG, "Battery alert state changed! Level: %d", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_BATTERY_ALERT, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 1)) {
        Level = ((In0 & (1 << 1)) != 0);
        ESP_LOGI(TAG, "Battery charging state changed! Level: %d", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_BATTERY_CHARGING, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 5)) {
        ESP_LOGI(TAG, "RTC interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_RTC_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status0 & (1 << 7)) {
        ESP_LOGI(TAG, "Temperature interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_TEMP_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status1 & (1 << 0)) {
        ESP_LOGI(TAG, "Range interrupt detected!");

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_RANGE_INTERRUPT, NULL, 0, pdMS_TO_TICKS(100));
    }

    if (Status1 & (1 << 4)) {
        /* SD detect: active low, no HW inversion → bit=0 in In1 means card inserted */
        Level = ((In1 & (1 << 4)) == 0);
        ESP_LOGI(TAG, "SD card detect changed! Inserted: %d", static_cast<int>(Level));

        esp_event_post(DEVICES_EVENTS, DEVICES_EVENT_SD_DETECT, &Level, sizeof(Level), pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

esp_err_t DevicesManager_SetLeptonPower(bool Enable)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_0, 2, Enable);
}

esp_err_t DevicesManager_SetCameraReset(bool Reset)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Camera reset is active low: assert reset when Reset=true → drive pin low */
    return PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Mainboard,
                                PCAL6416_PORT_1, 6, (Reset == false));
}

esp_err_t DevicesManager_EnableCamera(bool Enable)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_1, 7, Enable);
}

esp_err_t DevicesManager_GetBatteryAlert(bool *p_Alert)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Battery alert is HW polarity-inverted: ReadPin returns true when alert is active (pin low) */
    return PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_0, 0, p_Alert);
}

esp_err_t DevicesManager_GetBatteryCharging(bool *p_Charging)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    return PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_0, 1, p_Charging);
}

esp_err_t DevicesManager_GetRTCInterrupt(bool *p_Triggered)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t Error;
    bool Level;

    Error = PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_0, 5, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Active low: pin low (Level == false) means interrupt is asserted */
    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetTempInterrupt(bool *p_Triggered)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t Error;
    bool Level;

    Error = PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_0, 7, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Active low: pin low (Level == false) means interrupt is asserted */
    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetRangeInterrupt(bool *p_Triggered)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t Error;
    bool Level;

    Error = PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_1, 0, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    /* Active low: pin low (Level == false) means interrupt is asserted */
    *p_Triggered = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetSDDetect(bool *p_Inserted)
{
    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t Error;
    bool Level;

    Error = PCAL6416AHF_ReadPin(&_Devices_Manager_State.Expander_Mainboard, PCAL6416_PORT_1, 4, &Level);
    if (Error != ESP_OK) {
        return Error;
    }

    /* SD detect is active low: pin low (Level == false) means card is inserted */
    *p_Inserted = (Level == false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetDistance(uint16_t *p_Distance_mm, bool *p_IsValid)
{
    esp_err_t Error;
    bool Ready = false;
    VL53L1X_Result_t Result;

    if ((p_Distance_mm == NULL) || (p_IsValid == NULL)) {
        return ESP_ERR_INVALID_ARG;
    } else if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Sensor runs in continuous mode. Poll until the next measurement is ready
     * (inter-measurement period = 100 ms, timeout = 50 × 10 ms = 500 ms max).
     * VL53L1X_ClearInterrupt() re-arms the sensor for the next cycle after reading. */
    for (uint32_t Retry = 0; Retry < 50; Retry++) {
        /* Reset the task WDT on each retry: the loop can run for up to 500 ms which
         * would otherwise push the calling task close to the 5-second WDT boundary
         * when combined with other work in the same iteration. */
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));

        Error = VL53L1X_IsDataReady(&_Devices_Manager_State.VL53L1X, &Ready);
        if (Error != ESP_OK) {
            return Error;
        }

        if (Ready) {
            break;
        }
    }

    if (Ready == false) {
        VL53L1X_ClearInterrupt(&_Devices_Manager_State.VL53L1X);

        return ESP_ERR_TIMEOUT;
    }

    Error = VL53L1X_GetResult(&_Devices_Manager_State.VL53L1X, &Result);
    VL53L1X_ClearInterrupt(&_Devices_Manager_State.VL53L1X);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read VL53L1X result: 0x%x!", Error);

        return Error;
    }

    *p_Distance_mm = Result.Distance_mm;
    *p_IsValid = (Result.Status == VL53L1X_RANGE_VALID);

    ESP_LOGD(TAG, "Measured distance: %u mm, Status: %u, Signal rate: %u MCPS, Ambient rate: %u MCPS, SPADs: %u",
             Result.Distance_mm, Result.Status, Result.SignalRateMCPS, Result.AmbientRateMCPS, Result.EffectiveSPADs);

    return ESP_OK;
}

esp_err_t DevicesManager_SetLED(bool R, bool G, bool B)
{
    esp_err_t Error;

    if (_Devices_Manager_State.initialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    /* LEDs are active low: negate the logical level before writing to the output register */
    Error = PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Displayboard, PCAL6416_PORT_0, 0, (B == false));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write LED pin (B): 0x%x!", Error);

        return Error;
    }

    Error = PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Displayboard, PCAL6416_PORT_0, 1, (G == false));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write LED pin (G): 0x%x!", Error);

        return Error;
    }

    Error = PCAL6416AHF_WritePin(&_Devices_Manager_State.Expander_Displayboard, PCAL6416_PORT_0, 2, (R == false));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write LED pin (R): 0x%x!", Error);

        return Error;
    }

    return ESP_OK;
}