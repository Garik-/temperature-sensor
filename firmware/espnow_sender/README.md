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