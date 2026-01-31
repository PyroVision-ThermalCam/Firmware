/*
 * devicesManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Devices management implementation.
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
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <string.h>

#include "lepton.h"
#include "devices.h"
#include "devicesManager.h"
#include "I2C/i2c.h"
#include "SPI/spi.h"
#include "ADC/adc.h"
#include "RTC/rtc.h"
#include "PortExpander/portexpander.h"
#include "../Time/timeManager.h"

/** @brief Voltage divider resistors for battery measurement.
 */
#define BATTERY_R1                          10000   /* Upper resistor */
#define BATTERY_R2                          3300    /* Lower resistor */

/** @brief Battery voltage range (in millivolts).
 */
#define BATTERY_MIN_VOLTAGE                 3300    /* 3.3V */
#define BATTERY_MAX_VOLTAGE                 4200    /* 4.2V (fully charged LiPo) */

/** @brief Default configuration for the I2C interface.
 */
static i2c_master_bus_config_t _Devices_Manager_I2CM_Config = {
    .i2c_port = static_cast<i2c_port_t>(CONFIG_DEVICES_I2C_HOST),
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

/** @brief Default configuration for the SPI3 bus (shared by LCD, Touch, SD card).
 */
static const spi_bus_config_t _Devices_Manager_SPI_Config = {
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

typedef struct {
    bool initialized;
    i2c_master_dev_handle_t RTC_Handle;
    i2c_master_bus_handle_t I2C_Bus_Handle;
} Devices_Manager_State_t;

static Devices_Manager_State_t _Devices_Manager_State;

static const char *TAG = "devices-manager";

esp_err_t DevicesManager_Init(void)
{
    if (_Devices_Manager_State.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (I2CM_Init(&_Devices_Manager_I2CM_Config, &_Devices_Manager_State.I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C!");
        return ESP_FAIL;
    }

    if (SPIM_Init(&_Devices_Manager_SPI_Config, SPI3_HOST, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI3!");
        return ESP_FAIL;
    }

    if (PortExpander_Init(&_Devices_Manager_I2CM_Config, &_Devices_Manager_State.I2C_Bus_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default configuration for port expander!");
        return ESP_FAIL;
    }

    /* Give the camera time to power up and stabilize (Lepton requires ~1.5s boot time) */
    ESP_LOGI(TAG, "Waiting for camera power stabilization...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    if (RTC_Init(&_Devices_Manager_I2CM_Config, &_Devices_Manager_State.I2C_Bus_Handle,
                 &_Devices_Manager_State.RTC_Handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RTC!");
        return ESP_FAIL;
    }

    PortExpander_EnableBatteryVoltage(false);

    _Devices_Manager_State.initialized = true;

    return ESP_OK;
}

esp_err_t DevicesManager_Deinit(void)
{
    esp_err_t Error;

    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices manager not initialized yet!");
        return ESP_OK;
    }

    PortExpander_EnableBatteryVoltage(false);

    ADC_Deinit();
    RTC_Deinit();
    PortExpander_Deinit();
    Error = I2CM_Deinit(_Devices_Manager_State.I2C_Bus_Handle);

    SPIM_Deinit(SPI3_HOST);

    _Devices_Manager_State.initialized = false;

    return Error;
}

i2c_master_bus_handle_t DevicesManager_GetI2CBusHandle(void)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices manager not initialized yet!");
        return NULL;
    }

    return _Devices_Manager_State.I2C_Bus_Handle;
}

esp_err_t DevicesManager_GetBatteryVoltage(int *p_Voltage, uint8_t *p_Percentage)
{
    esp_err_t Error;

    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");
        return ESP_ERR_INVALID_STATE;
    } else if ((p_Voltage == NULL) || (p_Percentage == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    PortExpander_EnableBatteryVoltage(true);

    Error = ADC_ReadBattery(p_Voltage, p_Percentage);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %d", Error);

        PortExpander_EnableBatteryVoltage(false);

        return Error;
    }

    PortExpander_EnableBatteryVoltage(false);

    return ESP_OK;
}

esp_err_t DevicesManager_GetRTCHandle(i2c_master_dev_handle_t *p_Handle)
{
    if (_Devices_Manager_State.initialized == false) {
        ESP_LOGE(TAG, "Devices Manager not initialized yet!");
        return ESP_ERR_INVALID_STATE;
    }

    if (p_Handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *p_Handle = _Devices_Manager_State.RTC_Handle;

    return ESP_OK;
}
