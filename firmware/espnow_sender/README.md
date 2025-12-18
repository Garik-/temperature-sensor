Для того что бы отправить данные в esp_now нужно вызвать esp_now_send, но это операция асинхронная и поэтому нужно после этого вызова дождаться espnow_send_cb статуса о том что транзакция была отправлена или не отправлена
при этом espnow_send_cb должен быть максимально быстрым, от этого зависит скорость передачи данных по сети, поэтому в нем обычно просто записывают статус в очередь, а дальше ждут статуса в отдельной задаче

## Приоритеты задач
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html#fixed-priority

0 низкая 25 самая высокая, если поставить слишком большой приоритет то будет сжирать весь CPU
Обычно пользовательские задачи используют приоритет 1…10 для обычных действий, 4–5 для "средних", больше 10 для критичных задач (таймеры, Wi-Fi, ISR обработка).

## Примитивы синхронизации
Про очереди мы уже знаем, есть бинарные симафоры и мьютексы

Если мы хотим нотификацию сделать что типа подожди пока в другой таске что-то там сработает - то это семафор
https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications

оказывается есть вот такой вот функционал и типа он круче чем бинарные семафоры

The flexibility of task notifications allows them to be used where otherwise it would have been necessary to create a separate queue, binary semaphore, counting semaphore or event group. Unblocking an RTOS task with a direct notification is 45% faster† and uses less RAM than unblocking a task using an intermediary object such as a binary semaphore

## Deep sleep
В общем есть 

```
ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
```
это про light sleep типа wifi переодически просыпается и отправлять данные
а есть deep sleep

это когда запускается app_main исполняется вызываются функции когда проснутся и функция погружения

что бы это работало нормально мне надо написать программу так что бы она исполнялась линейно
ждать завершения асинхронных задача можно через task notify семафоры

```
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TaskNotificationExample";
static TaskHandle_t main_task_handle = NULL; // Хэндл главной задачи
static const int NUM_WORKER_TASKS = 3;       // Количество задач

void worker_task(void *params) {
    int task_id = (int)params;

    ESP_LOGI(TAG, "Worker Task %d started", task_id);

    // Имитация работы
    vTaskDelay(pdMS_TO_TICKS(500 + task_id * 100));

    ESP_LOGI(TAG, "Worker Task %d completed", task_id);

    // Уведомляем главную задачу, что текущая задача завершена
    xTaskNotifyGive(main_task_handle);

    // Завершаем задачу
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "App started");

    // Получаем хэндл главной задачи
    main_task_handle = xTaskGetCurrentTaskHandle();

    // Создаем worker задачи
    for (int i = 0; i < NUM_WORKER_TASKS; i++) {
        xTaskCreate(worker_task, "WorkerTask", 2048, (void *)i, 5, NULL);
    }

    // Ожидаем завершения всех worker задач
    for (int i = 0; i < NUM_WORKER_TASKS; i++) {
        // Ожидаем уведомление от одной из задач
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Received notification from worker task %d", i);
    }

    ESP_LOGI(TAG, "All worker tasks completed!");
}
```

главное корректно освобожать ресурсы и перед засыпанием все выключить wifi, gpio и тд и тп
