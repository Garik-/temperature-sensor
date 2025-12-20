#include "wait_group.h"

struct wait_group_t {
    TaskHandle_t owner; /* task that is currently waiting (set in wg_wait) */
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
    configASSERT(delta > 0); /* require positive adds */

    /* atomically add */
    __atomic_add_fetch(&wg->counter, delta, __ATOMIC_SEQ_CST);
}

void wg_done(wait_group_handle_t wg) {
    configASSERT(wg);

    /* atomically decrement and get previous value */
    int prev = __atomic_fetch_sub(&wg->counter, 1, __ATOMIC_SEQ_CST);

    if (prev <= 0) {
        /* too many done calls — bad usage */
        ESP_LOGE("wg", "wg_done called too many times (prev=%d)", prev);
        return;
    }

    ESP_LOGD("wg", "wg_done prev=%d", prev);

    /* if previous was 1, counter became 0 — notify the waiter */
    if (prev == 1) {
        TaskHandle_t owner = __atomic_load_n(&wg->owner, __ATOMIC_SEQ_CST);
        if (owner) {
            xTaskNotifyGive(owner);
        } else {
            /* No owner set yet — possible misuse: last done happened before waiter started.
               Either ensure wg_wait is called before starting workers, or change API to
               store a persistent signal (e.g. EventGroup) */
            ESP_LOGW("wg", "last wg_done but no owner to notify (lost notify)");
        }
    }
}

void wg_wait(wait_group_handle_t wg) {
    configASSERT(wg);

    /* fast-path: if counter is zero now, return immediately */
    int cnt = __atomic_load_n(&wg->counter, __ATOMIC_SEQ_CST);
    if (cnt == 0) {
        /* reset owner and return */
        __atomic_store_n(&wg->owner, NULL, __ATOMIC_SEQ_CST);
        return;
    }

    /* wait for notification that counter reached zero */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* ensure counter is zero and clear owner */
    __atomic_store_n(&wg->counter, 0, __ATOMIC_SEQ_CST);
}

void wg_destroy(wait_group_handle_t wg) {
    if (!wg)
        return;
    free(wg);
}