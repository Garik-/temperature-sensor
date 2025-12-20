
// Делаем линейную логику для deep sleep

#include "bat_adc.h"
#include "bme280.h"
#include "closer.h"
#include "driver/gpio.h"
#include "esp_assert.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus.h"
#include "nvs_flash.h"
#include "protocol.h"
#include "wait_group.h"

#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define ESPNOW_QUEUE_SIZE 5
#define ESPNOW_MAXDELAY pdMS_TO_TICKS(512)

#define BME_SDA GPIO_NUM_5        // GPIO 5 (SDA)
#define BME_SCL GPIO_NUM_6        // GPIO 6 (SCL)
#define BME_OUTPUT_PWR GPIO_NUM_4 // GPIO 4 (HIGH)

#define BAT_ADC_UNIT ADC_UNIT_1
#define BAT_ADC_CHAN ADC_CHANNEL_3
#define BAT_ADC_ATTEN ADC_ATTEN_DB_12

#define SEND_PACKET ESPNOW_QUEUE_SIZE
#define SEND_PACKET_INTERVAL pdMS_TO_TICKS(100)

#define WARMUP_MS pdMS_TO_TICKS(2)

#define uS_TO_S_FACTOR 1000000ULL
#define SLEEP_INTERVAL (10 * 60 * uS_TO_S_FACTOR) // seconds

#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0

#define GPIO_OUTPUT_PIN_SEL ((1ULL << BME_OUTPUT_PWR))

#define DEVICE_MAC {0x02, 0x12, 0x34, 0x56, 0x78, 0x9A}

static const char *TAG = "esp_now_sender";

static closer_handle_t s_closer = NULL;
#define DEFER(fn) CLOSER_DEFER(s_closer, (void *)fn)

static QueueHandle_t s_event_queue = NULL;
static i2c_bus_handle_t i2c_bus = NULL;
static bme280_handle_t bme280 = NULL;
static wait_group_handle_t wg = NULL;

static TaskHandle_t xTaskToNotify = NULL;

/* WiFi should start before using ESPNOW */
static esp_err_t wifi_init(uint8_t channel, const uint8_t *mac) {
    ESP_LOGI(TAG, "wifi_init: channel=%d mac=" MACSTR, channel, MAC2STR(mac));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), TAG, "esp_wifi_set_channel failed");

    if (mac != NULL) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mac(WIFI_IF_STA, mac), TAG, "esp_wifi_set_mac failed");
    }

    return ESP_OK;
}

static void wifi_deinit() {
    esp_wifi_stop();
    esp_wifi_deinit();

    ESP_LOGD(TAG, "wifi_deinit");
}

static void event_queue_create() {
    s_event_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(test_espnow_data_t));
    configASSERT(s_event_queue);
}

static void event_queue_delete() {
    vQueueDelete(s_event_queue);
    s_event_queue = NULL;
}

static void wg_delete() {
    wg_destroy(wg);
    wg = NULL;
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
        gpio_set_level(BME_OUTPUT_PWR, 0);
        return ESP_FAIL;
    }

    bme280 = bme280_create(i2c_bus, BME280_I2C_ADDRESS_DEFAULT);
    if (bme280 == NULL) {
        ESP_LOGE(TAG, "bme280_create failed");
        i2c_bus_delete(&i2c_bus);
        gpio_set_level(BME_OUTPUT_PWR, 0);
        return ESP_FAIL;
    }

    vTaskDelay(WARMUP_MS);
    ESP_RETURN_ON_ERROR(bme280_default_init(bme280), TAG, "bme280_default_init failed");

    return ESP_OK;
}

static void bme280_deinit() {
    bme280_delete(&bme280);
    i2c_bus_delete(&i2c_bus);
    gpio_set_level(BME_OUTPUT_PWR, 0);
    ESP_LOGD(TAG, "bme280_deinit");
}

static void bme280_task(void *pvParameter) {
    if (ESP_OK != bme280_init()) {
        ESP_LOGE(TAG, "BME280 init failed");

        wg_done(wg);
        vTaskDelete(NULL);
        return;
    }

    adc_oneshot_unit_handle_t oneshot_unit_handle = NULL;
    adc_cali_handle_t cali_handle = NULL;

    if (ESP_OK != bat_adc_init(BAT_ADC_UNIT, BAT_ADC_CHAN, BAT_ADC_ATTEN, &oneshot_unit_handle, &cali_handle)) {
        ESP_LOGE(TAG, "bat_adc_init_failed");

        bme280_deinit();
        wg_done(wg);
        vTaskDelete(NULL);
        return;
    }

    float value;

    test_espnow_data_t data;
    data.start_flag = START_FLAG;

    int cnt = SEND_PACKET;
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

        if (xQueueSend(s_event_queue, &data, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "xQueueSend failed");
            break;
        }
        vTaskDelay(SEND_PACKET_INTERVAL);
    }

    bat_adc_deinit(oneshot_unit_handle, cali_handle);
    bme280_deinit();

    wg_done(wg);
    vTaskDelete(NULL);
}

static void test_espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    configASSERT(xTaskToNotify != NULL);
    xTaskNotifyFromISR(xTaskToNotify, status, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void test_espnow_task(void *peer_addr) {
    test_espnow_data_t data;

    xTaskToNotify = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "start sending data");

    BaseType_t xResult;
    uint32_t status;

    int cnt = SEND_PACKET;
    while (cnt--) {
        if (xQueueReceive(s_event_queue, &data, ESPNOW_MAXDELAY) == pdTRUE) {
            if (esp_now_send((const uint8_t *)peer_addr, (const uint8_t *)&data, sizeof(test_espnow_data_t)) !=
                ESP_OK) {
                ESP_LOGE(TAG, "Send error");

                wg_done(wg);
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

    ESP_LOGI(TAG, "stop sending data");

    wg_done(wg);
    vTaskDelete(NULL);
}

static esp_err_t test_espnow_init(const uint8_t *peer_addr) {

    ESP_LOGI(TAG, "esp_now_init peer=" MACSTR, MAC2STR(peer_addr));

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init fail");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(test_espnow_send_cb), TAG, "esp_now_register_send_cb fail");

    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, peer_addr, ESP_NOW_ETH_ALEN);

    if (esp_now_add_peer(peer) != ESP_OK) {
        ESP_LOGE(TAG, "Add peer fail");
        esp_now_deinit();
        free(peer);
        return ESP_FAIL;
    }

    free(peer);

    return ESP_OK;
}

static void enter_deep_sleep() {
    if (SLEEP_INTERVAL <= 0)
        return;

    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL);
    esp_deep_sleep_start();
}

// TODO: надо моргать при ошибки и потом входить в слип

void app_main(void) {
    ESP_ERROR_CHECK(closer_create(&s_closer));

    ESP_ERROR_CHECK(wg_create(&wg));
    DEFER(wg_delete);

    event_queue_create();
    DEFER(event_queue_delete);

    ESP_LOGD(TAG, "nvs_flash_init");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ESP_OK == ret) { // nvs_init
        DEFER(nvs_flash_deinit);

        wg_add(wg, 1);
        xTaskCreate(bme280_task, "bme280_task", 4096, NULL, 3, NULL);

        const uint8_t device_mac[ESP_NOW_ETH_ALEN] = DEVICE_MAC;
        ret = wifi_init(CONFIG_ESPNOW_CHANNEL, device_mac);
        DEFER(wifi_deinit);

        if (ESP_OK == ret) { // wifi_init

            const uint8_t peer_addr[ESP_NOW_ETH_ALEN] = {0xa0, 0xb7, 0x65, 0x15, 0x84, 0x45}; // a0:b7:65:15:84:45

            if (ESP_OK == test_espnow_init(peer_addr)) {
                DEFER(esp_now_deinit);

                wg_add(wg, 1);
                xTaskCreate(test_espnow_task, "test_espnow_task", 2048, (void *const)peer_addr, 4, NULL);
            } else {
                ESP_LOGE(TAG, "test_espnow_init failed");
            }

        } else {
            ESP_LOGE(TAG, "wifi_init failed");
        }
    } else {
        ESP_LOGE(TAG, "nvs_flash_init failed");
    }

    ESP_LOGI(TAG, "wait tasks");
    wg_wait(wg);

    closer_close(s_closer);
    closer_destroy(s_closer);

    // enter_deep_sleep();
}