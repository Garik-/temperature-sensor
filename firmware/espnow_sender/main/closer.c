#include "closer.h"

typedef struct closer_item {
    closer_fn_t fn;
    struct closer_item *next;
} closer_item_t;

struct closer_t {
    closer_item_t *top;
};

esp_err_t closer_create(closer_handle_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;

    struct closer_t *c = calloc(1, sizeof(*c));
    if (!c)
        return ESP_ERR_NO_MEM;

    *out = c;
    return ESP_OK;
}

void closer_destroy(closer_handle_t h) {
    if (!h)
        return;

    closer_close(h);
    free(h);
}

esp_err_t closer_add(closer_handle_t h, closer_fn_t fn) {
    if (!h || !fn)
        return ESP_ERR_INVALID_ARG;

    closer_item_t *item = malloc(sizeof(*item));
    if (!item)
        return ESP_ERR_NO_MEM;

    item->fn = fn;
    item->next = h->top;
    h->top = item;

    return ESP_OK;
}

void closer_close(closer_handle_t h) {
    if (!h)
        return;

    closer_item_t *item = h->top;
    while (item) {
        if (item->fn)
            item->fn();

        closer_item_t *tmp = item;
        item = item->next;
        free(tmp);
    }

    h->top = NULL;
}
