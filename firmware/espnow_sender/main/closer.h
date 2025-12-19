#ifndef _CLOSER_H_
#define _CLOSER_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*closer_fn_t)(void);
typedef struct closer_t *closer_handle_t;

esp_err_t closer_create(closer_handle_t *out);
void closer_destroy(closer_handle_t h);

esp_err_t closer_add(closer_handle_t h, closer_fn_t fn);
void closer_close(closer_handle_t h);

#define CLOSER_DEFER(h, fn)                                                                                            \
    do {                                                                                                               \
        closer_fn_t _fn = (fn);                                                                                        \
        closer_add((h), _fn);                                                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif