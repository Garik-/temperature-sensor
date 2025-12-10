#include "bme280.h"
#include "driver/gpio.h"
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
#include <assert.h>
#include <stdlib.h>

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define ESPNOW_QUEUE_SIZE 6
#define ESPNOW_MAXDELAY 512

#define BME_SDA GPIO_NUM_5        // GPIO 5 (SDA)
#define BME_SCL GPIO_NUM_6        // GPIO 6 (SCL)
#define BME_OUTPUT_PWR GPIO_NUM_4 // GPIO 4 (HIGH)

#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0

#define GPIO_OUTPUT_PIN_SEL ((1ULL << BME_OUTPUT_PWR))

typedef enum {
    SEND_CB,
    RECV_CB,
} event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    esp_now_send_status_t status;
} event_send_cb_t;

typedef union {
    event_send_cb_t send_cb;
} event_info_t;

typedef struct {
    event_id_t id;
    event_info_t info;
} event_t;

static const char *TAG = "esp_now_sender";
static uint8_t s_mac[ESP_NOW_ETH_ALEN] = {0x02, 0x12, 0x34, 0x56, 0x78, 0x9A};
static uint8_t s_peer_mac[ESP_NOW_ETH_ALEN] = {0xa0, 0xb7, 0x65, 0x15, 0x84, 0x45}; // a0:b7:65:15:84:45
static QueueHandle_t s_event_queue = NULL;

static i2c_bus_handle_t i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;

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
    esp_now_deinit();
}

static void test_espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    event_t evt;
    event_send_cb_t *send_cb = &evt.info.send_cb;

    if (tx_info == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = SEND_CB;
    memcpy(send_cb->mac_addr, tx_info->des_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    if (xQueueSend(s_event_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void test_espnow_task(void *pvParameter) {
    event_t evt;

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Start sending data");

    if (esp_now_send((const uint8_t *)&s_peer_mac, (const uint8_t *)&s_mac, ESP_NOW_ETH_ALEN) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        test_espnow_deinit();
        vTaskDelete(NULL);
    }

    while (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {

        if (evt.id != SEND_CB) {
            ESP_LOGW(TAG, "receive invalid event id: %d", evt.id);
            continue;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        /* Send the next data after the previous data is sent. */
        if (esp_now_send((const uint8_t *)&s_peer_mac, (const uint8_t *)&s_mac, ESP_NOW_ETH_ALEN) != ESP_OK) {
            ESP_LOGE(TAG, "Send error");
            test_espnow_deinit();
            vTaskDelete(NULL);
        }
    }
}

static esp_err_t test_espnow_init(void) {

    s_event_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "create queue fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(test_espnow_send_cb));

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
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    xTaskCreate(test_espnow_task, "test_espnow_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}

static esp_err_t bme280_init() {
    esp_err_t ret;

    gpio_config_t io_conf = {.pin_bit_mask = GPIO_OUTPUT_PIN_SEL, .mode = GPIO_MODE_OUTPUT};
    ret = gpio_config(&io_conf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_level(BME_OUTPUT_PWR, 1);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_level failed: %s", esp_err_to_name(ret));
        return ret;
    }

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

    ret = bme280_default_init(bme280);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bme280_default_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t bme280_deinit() {
    bme280_delete(&bme280);
    i2c_bus_delete(&i2c_bus);

    gpio_set_level(BME_OUTPUT_PWR, 0);
}

void bme280_test_getdata() {
    int cnt = 10;
    while (cnt--) {
        float temperature = 0.0, humidity = 0.0, pressure = 0.0;
        if (ESP_OK == bme280_read_temperature(bme280, &temperature)) {
            ESP_LOGI(TAG, "temperature:%f ", temperature);
        }
        vTaskDelay(300 / portTICK_RATE_MS);
        if (ESP_OK == bme280_read_humidity(bme280, &humidity)) {
            ESP_LOGI(TAG, "humidity:%f ", humidity);
        }
        vTaskDelay(300 / portTICK_RATE_MS);
        if (ESP_OK == bme280_read_pressure(bme280, &pressure)) {
            ESP_LOGI(TAG, "pressure:%f\n", pressure);
        }
        vTaskDelay(300 / portTICK_RATE_MS);
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

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(bme280_init());

    bme280_test_getdata();
    ESP_LOGI(TAG, "BME280 data read complete");
}