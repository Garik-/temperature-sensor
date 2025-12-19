#include "wait_group.h"

struct wait_group_t {
    TaskHandle_t owner;
    int counter;
};

esp_err_t wg_create(wait_group_handle_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;

    struct wait_group_t *wg = calloc(1, sizeof(*wg));
    if (!wg)
        return ESP_ERR_NO_MEM;

    wg->owner = xTaskGetCurrentTaskHandle();
    wg->counter = 0;

    *out = wg;
    return ESP_OK;
}

void wg_add(wait_group_handle_t wg, int delta) {
    configASSERT(wg);
    wg->counter += delta;
}

void wg_done(wait_group_handle_t wg) {
    configASSERT(wg);
    xTaskNotifyGive(wg->owner);
}

void wg_wait(wait_group_handle_t wg) {
    configASSERT(wg);

    for (int i = 0; i < wg->counter; i++) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    wg->counter = 0;
}

void wg_destroy(wait_group_handle_t wg) {
    if (!wg)
        return;
    free(wg);
}
