#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include <assert.h>
#include <stdlib.h>

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define ESPNOW_QUEUE_SIZE 6
#define ESPNOW_MAXDELAY 512

typedef enum {
    SEND_CB,
    RECV_CB,
} event_id_t;

typedef struct {
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} event_recv_cb_t;

typedef union {
    event_recv_cb_t recv_cb;
} event_info_t;

typedef struct {
    event_id_t id;
    event_info_t info;
} event_t;

static const char *TAG = "esp_now_receiver";
static QueueHandle_t s_event_queue = NULL;
static uint8_t s_peer_mac[ESP_NOW_ETH_ALEN] = {0x02, 0x12, 0x34, 0x56, 0x78, 0x9A};

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
}

static void test_espnow_deinit() {
    vQueueDelete(s_event_queue);
    s_event_queue = NULL;
    esp_now_deinit();
}

static void test_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    event_t evt;
    event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t *mac_addr = recv_info->src_addr;
    // uint8_t *des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    // TODO: add check dest_addr if needed

    evt.id = RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    if (xQueueSend(s_event_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

static void test_espnow_task(void *pvParameter) {

    event_t evt;

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "start receive peer data task");

    while (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
        if (evt.id != RECV_CB) {
            ESP_LOGW(TAG, "receive invalid event id: %d", evt.id);
            continue;
        }

        event_recv_cb_t *recv_cb = &evt.info.recv_cb;
        ESP_LOGI(TAG, "receive data from " MACSTR ", len: %d", MAC2STR(recv_cb->mac_addr), recv_cb->data_len);
        free(recv_cb->data);
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
    ESP_ERROR_CHECK(esp_now_register_recv_cb(test_espnow_recv_cb));

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
}