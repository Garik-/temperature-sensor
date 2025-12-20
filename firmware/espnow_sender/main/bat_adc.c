#include "bat_adc.h"

const static char *TAG = "bat_adc";

struct bat_adc_t {
    adc_oneshot_unit_handle_t oneshot_unit_handle;
    adc_cali_handle_t cali_handle;
    bool do_calibration;
}; // TODO: остановился на переписывании, вместо передачи кучи переменных передачи структуры

static bool bat_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                                     adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static esp_err_t bet_adc_calibration_deinit(adc_cali_handle_t handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    return adc_cali_delete_scheme_curve_fitting(handle);

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    return adc_cali_delete_scheme_line_fitting(handle);
#endif
}

esp_err_t bat_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *value) {
    int raw = 0;
    adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &raw);
    if (do_calibration) {
        adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]))
    }
}

esp_err_t bat_adc_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                       adc_oneshot_unit_handle_t *out_oneshot_unit_handle, adc_cali_handle_t *out_cali_handle) {

    adc_oneshot_unit_handle_t adc1_handle = NULL;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ESP_OK == ret) {
        adc_oneshot_chan_cfg_t config = {
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_oneshot_config_channel(adc1_handle, channel, &config);
        if (ESP_OK == ret) {

            *out_oneshot_unit_handle = adc1_handle;

            adc_cali_handle_t adc1_cali_chan_handle = NULL;
            do_calibration = bat_adc_calibration_init(unit, channel, atten, &adc1_cali_chan_handle);

            if (do_calibration) {
                *out_cali_handle = adc1_cali_chan_handle;
            }
        }
    }
    return ret;
}

esp_err_t bat_adc_deinit(adc_oneshot_unit_handle_t oneshot_unit_handle, adc_cali_handle_t cali_handle) {
    if (do_calibration && cali_handle != NULL) {
        if (ESP_OK != bet_adc_calibration_deinit(cali_handle)) {
            ESP_LOGE(TAG, "bet_adc_calibration_deinit failed");
        }
    }
    return adc_oneshot_del_unit(oneshot_unit_handle);
}