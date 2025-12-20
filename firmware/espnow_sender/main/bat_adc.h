#ifndef _BATADC_H_
#define _BATADC_H_

#include "err.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bat_adc_t *bat_adc_handle_t;

esp_err_t bat_adc_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, bat_adc_handle_t *out);
esp_err_t bat_adc_deinit(bat_adc_handle_t handle);
esp_err_t bat_read(bat_adc_handle_t handle, int *value);

#ifdef __cplusplus
}
#endif

#endif