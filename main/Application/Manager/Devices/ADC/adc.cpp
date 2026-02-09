/*
 * adc.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: ADC driver implementation.
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
 */

#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include "adc.h"

#include <sdkconfig.h>

#ifdef CONFIG_BATTERY_ADC_1
#define ADC_UNIT    ADC_UNIT_1
#elif CONFIG_BATTERY_ADC_2
#define ADC_UNIT    ADC_UNIT_2
#else
#error "No ADC channel configured for battery measurement"
#endif

#ifdef CONFIG_BATTERY_ADC_ATT_0
#define ADC_ATTEN   ADC_ATTEN_DB_0
#elif CONFIG_BATTERY_ADC_ATT_2_5
#define ADC_ATTEN   ADC_ATTEN_DB_2_5
#elif CONFIG_BATTERY_ADC_ATT_6
#define ADC_ATTEN   ADC_ATTEN_DB_6
#elif CONFIG_BATTERY_ADC_ATT_12
#define ADC_ATTEN   ADC_ATTEN_DB_12
#else
#error "No ADC attenuation configured for battery measurement"
#endif

static bool _ADC_Calib_Done = false;

static adc_channel_t _ADC_Channel = static_cast<adc_channel_t>(CONFIG_BATTERY_ADC_CHANNEL);
static adc_cali_handle_t _ADC_Calib_Handle;
static adc_oneshot_unit_handle_t _ADC_Handle;
static adc_oneshot_unit_init_cfg_t _ADC_Init_Config = {
    .unit_id = ADC_UNIT,
    .clk_src = ADC_RTC_CLK_SRC_RC_FAST,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

static adc_oneshot_chan_cfg_t _ADC_Config = {
    .atten = static_cast<adc_atten_t>(ADC_ATTEN),
    .bitwidth = ADC_BITWIDTH_DEFAULT,
};

const char *TAG = "ADC";

esp_err_t ADC_Init(void)
{
    esp_err_t Error;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&_ADC_Init_Config, &_ADC_Handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(_ADC_Handle, _ADC_Channel, &_ADC_Config));

    Error = ESP_OK;
    _ADC_Calib_Done = false;

    if (_ADC_Calib_Done == false) {
        ESP_LOGD(TAG, "Calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t CaliConfig = {
            .unit_id = _ADC_Init_Config.unit_id,
            .chan = _ADC_Channel,
            .atten = _ADC_Config.atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };

        Error = adc_cali_create_scheme_curve_fitting(&CaliConfig, &_ADC_Calib_Handle);
        if (Error == ESP_OK) {
            _ADC_Calib_Done = true;
        }
    }

    if (Error == ESP_OK) {
        ESP_LOGD(TAG, "Calibration Success");
    } else if ((Error == ESP_ERR_NOT_SUPPORTED) || (_ADC_Calib_Done == false)) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory!");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "ADC Initialized");

    return ESP_OK;
}

esp_err_t ADC_Deinit(void)
{
    ESP_ERROR_CHECK(adc_oneshot_del_unit(_ADC_Handle));

    if (_ADC_Calib_Done) {
        return adc_cali_delete_scheme_curve_fitting(_ADC_Calib_Handle);
    }

    return ESP_OK;
}

esp_err_t ADC_ReadBattery(int *p_Voltage, uint8_t *p_Percentage)
{
    int Raw;

    if ((p_Voltage == NULL) || (p_Percentage == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(ADC_Init());

    ESP_ERROR_CHECK(adc_oneshot_read(_ADC_Handle, _ADC_Channel, &Raw));

    if (_ADC_Calib_Done) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(_ADC_Calib_Handle, Raw, p_Voltage));
        *p_Voltage = (*p_Voltage) * (CONFIG_BATTERY_ADC_R1 + CONFIG_BATTERY_ADC_R2) / CONFIG_BATTERY_ADC_R2;
    } else {
        *p_Voltage = Raw;
    }

    /* Calculate battery percentage (LiPo: 3.0V = 0%, 4.2V = 100%) */
    if (*p_Voltage <= CONFIG_BATTERY_ADC_MIN_MV) {
        *p_Percentage = 0;
    } else if (*p_Voltage >= CONFIG_BATTERY_ADC_MAX_MV) {
        *p_Percentage = 100;
    } else {
        *p_Percentage = (uint8_t)((*p_Voltage - CONFIG_BATTERY_ADC_MIN_MV) * 100 / (CONFIG_BATTERY_ADC_MAX_MV -
                                                                                    CONFIG_BATTERY_ADC_MIN_MV));
    }

    ESP_LOGD(TAG, "ADC%d Channel%d raw data: %d", ADC_UNIT_1, _ADC_Channel, Raw);
    ESP_LOGD(TAG, "ADC%d Channel%d battery: %d mV (%d%%)", ADC_UNIT_1, _ADC_Channel, *p_Voltage, *p_Percentage);

    return ADC_Deinit();
}