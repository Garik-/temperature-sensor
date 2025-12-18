#include "bme280.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus.h"
#include "nvs_flash.h"
#include "protocol.h"
#include <assert.h>
#include <stdlib.h>

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define ESPNOW_QUEUE_SIZE 6
#define ESPNOW_MAXDELAY pdMS_TO_TICKS(512)

#define BME_SDA GPIO_NUM_5        // GPIO 5 (SDA)
#define BME_SCL GPIO_NUM_6        // GPIO 6 (SCL)
#define BME_OUTPUT_PWR GPIO_NUM_4 // GPIO 4 (HIGH)

#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0

#define GPIO_OUTPUT_PIN_SEL ((1ULL << BME_OUTPUT_PWR))

static const char *TAG = "esp_now_sender";
static uint8_t s_mac[ESP_NOW_ETH_ALEN] = {0x02, 0x12, 0x34, 0x56, 0x78, 0x9A};
static uint8_t s_peer_mac[ESP_NOW_ETH_ALEN] = {0xa0, 0xb7, 0x65, 0x15, 0x84, 0x45}; // a0:b7:65:15:84:45

static i2c_bus_handle_t i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;

static TaskHandle_t xTaskToNotify = NULL;
static QueueHandle_t s_event_queue = NULL;

/* WiFi should start before using ESPNOW */
static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, s_mac));
}

static void test_espnow_deinit() {
    vQueueDelete(s_event_queue);
    s_event_queue = NULL;
    ESP_ERROR_CHECK(esp_now_deinit());
}

static void test_espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    configASSERT(xTaskToNotify != NULL);

    xTaskNotifyFromISR(xTaskToNotify, status, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void test_espnow_task(void *pvParameter) {
    test_espnow_data_t data;

    xTaskToNotify = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "Start sending data");

    BaseType_t xResult;
    uint32_t status;

    while (xQueueReceive(s_event_queue, &data, portMAX_DELAY) == pdTRUE) {
        if (esp_now_send((const uint8_t *)&s_peer_mac, (const uint8_t *)&data, sizeof(test_espnow_data_t)) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
            test_espnow_deinit();
            vTaskDelete(NULL);
        }

        xResult = xTaskNotifyWait(pdFALSE,   /* Don't clear bits on entry. */
                                  ULONG_MAX, /* Clear all bits on exit. */
                                  &status,   /* Stores the notified value. */
                                  ESPNOW_MAXDELAY);

        if (xResult != pdPASS) {
            ESP_LOGW(TAG, "No notification received within the timeout period");
        }
    }
}

static esp_err_t test_espnow_init(void) {
    s_event_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(test_espnow_data_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "create queue fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init fail");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(test_espnow_send_cb), TAG, "esp_now_register_send_cb fail");

    /* Add broadcast peer information to peer list. */
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        test_espnow_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, s_peer_mac, ESP_NOW_ETH_ALEN);

    if (esp_now_add_peer(peer) != ESP_OK) {
        ESP_LOGE(TAG, "Add peer fail");
        test_espnow_deinit();
        free(peer);
        return ESP_FAIL;
    }

    free(peer);
    xTaskCreate(test_espnow_task, "test_espnow_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}

static esp_err_t bme280_init() {
    gpio_config_t io_conf = {.pin_bit_mask = GPIO_OUTPUT_PIN_SEL, .mode = GPIO_MODE_OUTPUT};

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BME_OUTPUT_PWR, 1), TAG, "gpio_set_level failed");

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BME_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BME_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &conf);
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "i2c_bus_create failed");
        return ESP_FAIL;
    }

    bme280 = bme280_create(i2c_bus, BME280_I2C_ADDRESS_DEFAULT);
    if (bme280 == NULL) {
        ESP_LOGE(TAG, "bme280_create failed");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(bme280_default_init(bme280), TAG, "bme280_default_init failed");

    return ESP_OK;
}

static void bme280_deinit() {
    bme280_delete(&bme280);
    i2c_bus_delete(&i2c_bus);

    gpio_set_level(BME_OUTPUT_PWR, 0);
}

static void bme280_getdata_task(void *pvParameter) {
    test_espnow_data_t data;
    float value;

    if (ESP_OK != bme280_init()) {
        ESP_LOGE(TAG, "BME280 init failed");
        vTaskDelete(NULL);
        return;
    }

    data.start_flag = START_FLAG;

    for (;;) {
        int cnt = 10;
        while (cnt--) {
            if (ESP_OK == bme280_read_temperature(bme280, &value)) {
                data.payload.temperature = (int16_t)(value * 100);
            }
            if (ESP_OK == bme280_read_humidity(bme280, &value)) {
                data.payload.humidity = (uint16_t)(value * 100);
            }
            if (ESP_OK == bme280_read_pressure(bme280, &value)) {
                pack_pressure((uint32_t)(value * 100), data.payload.pressure);
            }

            data.crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)&data.payload, sizeof(data.payload));

            ESP_LOGD(TAG, "Temp: %d, Hum: %d %%, Pres: %d Pa, CRC: 0x%04X", data.payload.temperature,
                     data.payload.humidity, data.payload.pressure, data.crc);

            if (xQueueSend(s_event_queue, &data, portMAX_DELAY) != pdTRUE) {
                ESP_LOGE(TAG, "xQueueSend failed");
                bme280_deinit();
                vTaskDelete(NULL);
            }
            vTaskDelay(300 / portTICK_RATE_MS);
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    test_espnow_init();

    xTaskCreate(bme280_getdata_task, "bme280_getdata_task", 4096, NULL, 5, NULL);
}