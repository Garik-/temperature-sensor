#ifndef _ERRHANDLING_H_
#define _ERRHANDLING_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT)
/**
 * Macro which can be used to check the error code. If the code is not ESP_OK, it prints the message.
 */
#define ESP_LOG_ON_ERROR(x, log_tag, format, ...)                                                                      \
    do {                                                                                                               \
        (x);                                                                                                           \
    } while (0)
#else // !CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT

#if defined(__cplusplus) && (__cplusplus > 201703L)

/**
 * Macro which can be used to check the error code. If the code is not ESP_OK, it prints the message.
 */
#define ESP_LOG_ON_ERROR(x, log_tag, format, ...)                                                                      \
    do {                                                                                                               \
        esp_err_t err_rc_ = (x);                                                                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                                                                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__ __VA_OPT__(, ) __VA_ARGS__);                   \
        }                                                                                                              \
    } while (0)
#else // !(defined(__cplusplus) && (__cplusplus >  201703L))

/**
 * Macro which can be used to check the error code. If the code is not ESP_OK, it prints the message.
 */
#define ESP_LOG_ON_ERROR(x, log_tag, format, ...)                                                                      \
    do {                                                                                                               \
        esp_err_t err_rc_ = (x);                                                                                       \
        if (unlikely(err_rc_ != ESP_OK)) {                                                                             \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__);                               \
        }                                                                                                              \
    } while (0)

#endif // !(defined(__cplusplus) && (__cplusplus >  201703L))

#endif // !CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT

#ifdef __cplusplus
}
#endif

#endif // _ERRHANDLING_H_