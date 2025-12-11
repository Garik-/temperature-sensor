#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_config.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "test_utils.h"
#endif

/* Stores the handle of the task that will be notified when the
   transmission is complete. */
static TaskHandle_t xTaskToNotify = NULL;

/* The index within the target task's array of task notifications
   to use. */
const UBaseType_t xArrayIndex = 1;

static const char *TAG = "main_test";

void start_transmission(void) {
    configASSERT(xTaskToNotify == NULL);
    xTaskToNotify = xTaskGetCurrentTaskHandle();
    // esp_now_send
}

static void test_taskA(void *pvParameter) {
    ESP_LOGI(TAG, "test_taskA started");

    start_transmission();

    ESP_LOGI(TAG, "test_taskA completed");
    vTaskDelete(NULL); // task should delete itself
}

static void test_taskB(void *pvParameter) {
    ESP_LOGI(TAG, "test_taskB started");

    TEST_ASSERT_NOT_NULL(xTaskToNotify);

    ESP_LOGI(TAG, "test_taskB completed");

    vTaskDelete(NULL); // task should delete itself
}

TEST_CASE("task notify", "[main_test]") {
    xTaskCreate(test_taskA, "test_taskA", 2048, NULL, 4, NULL);
    xTaskCreate(test_taskB, "test_taskB", 2048, NULL, 5, NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting tests");

    unity_run_menu();
}