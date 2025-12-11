#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_config.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "test_utils.h"
#endif

static int add(int a, int b) {
    return a + b;
}

TEST_CASE("adding numbers works", "[math]") {
    TEST_ASSERT_EQUAL_INT(4, add(2, 2));
}

void app_main(void) {
    ESP_LOGI("TEST_APP", "Starting tests");
    unity_run_menu();
}