#include "bat_adc.h"

const static char *TAG = "bat_adc";

struct bat_adc_t {
    adc_oneshot_unit_handle_t oneshot_unit_handle;
    adc_cali_handle_t cali_handle;
    adc_channel_t channel;
    bool do_calibration;
};

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

esp_err_t bat_read(bat_adc_handle_t handle, int *value) {
    if (!handle || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->oneshot_unit_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    ESP_RETURN_ON_ERROR(adc_oneshot_read(handle->oneshot_unit_handle, handle->channel, &raw), TAG, "adc_oneshot_read");

    if (!handle->do_calibration) {
        *value = raw;
        return ESP_OK;
    }

    if (!handle->cali_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    int voltage = 0;
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(handle->cali_handle, raw, &voltage), TAG, "adc_cali_raw_to_voltage");
    *value = voltage;

    return ESP_OK;
}

esp_err_t bat_adc_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, bat_adc_handle_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;

    struct bat_adc_t *handle = calloc(1, sizeof(*handle));
    if (!handle)
        return ESP_ERR_NO_MEM;

    handle->channel = channel;

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };

    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &handle->oneshot_unit_handle), TAG, "adc_oneshot_new_unit");

    adc_oneshot_chan_cfg_t config = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(handle->oneshot_unit_handle, channel, &config), TAG,
                        "adc_oneshot_config_channel");

    handle->do_calibration = bat_adc_calibration_init(unit, channel, atten, &handle->cali_handle);

    *out = handle;
    return ESP_OK;
}

esp_err_t bat_adc_deinit(bat_adc_handle_t handle) {
    if (!handle)
        return ESP_ERR_INVALID_ARG;

    if (handle->do_calibration && handle->cali_handle) {
        ESP_LOG_ON_ERROR(bet_adc_calibration_deinit(handle->cali_handle), TAG, "bet_adc_calibration_deinit");
        handle->cali_handle = NULL;
    }

    if (handle->oneshot_unit_handle) {
        ESP_LOG_ON_ERROR(adc_oneshot_del_unit(handle->oneshot_unit_handle), TAG, "adc_oneshot_del_unit");
        handle->oneshot_unit_handle = NULL;
    }

    free(handle);
    return ESP_OK;
}