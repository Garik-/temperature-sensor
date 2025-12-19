#ifndef _WAIT_GROUP_H_
#define _WAIT_GROUP_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wait_group_t *wait_group_handle_t;

esp_err_t wg_create(wait_group_handle_t *out);
void wg_add(wait_group_handle_t wg, int delta);
void wg_done(wait_group_handle_t wg);
void wg_wait(wait_group_handle_t wg);
void wg_destroy(wait_group_handle_t wg);

#ifdef __cplusplus
}
#endif

#endif