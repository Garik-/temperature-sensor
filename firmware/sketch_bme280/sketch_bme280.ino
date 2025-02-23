#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>

#define LED_BUILTIN 8 // Internal LED pin is 8 as per schematic

// Настройка I2C
#define BME_SDA 5     // GPIO 5 (SDA)
#define BME_SCL 6     // GPIO 6 (SCL)
#define BME_ADDR 0x76 // Адрес BME280 по умолчанию 0x76 (может быть 0x77)

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 2           /* Time ESP32 will go to sleep (in seconds) */

// Настройки backoff
#define INITIAL_DELAY 200 // Начальная задержка в мс
#define MAX_DELAY 1000    // Максимальная задержка в мс
#define MAX_ATTEMPTS 5    // Максимальное количество попыток

#define DEBUG 1 // 1 - включить отладку, 0 - отключить
#if DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

#pragma pack(push, 1)
struct SensorData
{
  float temperature;
  float humidity;
  float pressure;
};
#pragma pack(pop)

// Настройки UDP
const char *udpAddress = "192.168.1.255"; // Широковещательный адрес
const int udpPort = 12345;                // Порт UDP-сервера

// Переменные для backoff
int attemptCount = 0;             // Счетчик попыток
int currentDelay = INITIAL_DELAY; // Текущая задержка

WiFiUDP udp;
Adafruit_BME280 bme;

// Флаги для управления состоянием
bool wifiConnected = false;

// Функция для настройки и перехода в Deep Sleep
void enterDeepSleep()
{
  // Настройка таймера для пробуждения
  // esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  // DEBUG_PRINTLN("Entering Deep Sleep for " + String(TIME_TO_SLEEP) + " seconds");

  // Переход в режим Deep Sleep
  // esp_deep_sleep_start();

  delay(2000);
}

// Функция для отправки данных и перехода в сон
void sendDataAndSleep()
{
  if (!wifiConnected)
  {
    return;
  }

  if (udp.beginPacket(udpAddress, udpPort))
  {
    struct SensorData data;
    data.temperature = bme.readTemperature();
    data.humidity = bme.readHumidity();
    data.pressure = bme.readPressure();

#if DEBUG
    DEBUG_PRINT(" temperature=" + String(data.temperature));
    DEBUG_PRINT(" humidity=" + String(data.humidity));
    DEBUG_PRINTLN(" pressure=" + String(data.pressure));
#endif
    udp.write((uint8_t *)&data, sizeof(data));
    udp.endPacket();
    DEBUG_PRINTLN("Broadcast UDP packet sent");
  }

  // Переход в режим Deep Sleep
  enterDeepSleep();
}

// Функция для обработки повторного подключения с backoff
void handleReconnect()
{
  if (attemptCount < MAX_ATTEMPTS)
  {
    // Увеличиваем задержку экспоненциально
    currentDelay = min(currentDelay * 2, MAX_DELAY);
    attemptCount++;
    DEBUG_PRINTLN("Attempt " + String(attemptCount) + " with delay " + String(currentDelay) + " ms");
    delay(currentDelay); // Ждем перед следующей попыткой
    WiFi.reconnect();    // Пытаемся переподключиться
  }
  else
  {
    // Если попытки исчерпаны, переходим в Deep Sleep
    DEBUG_PRINTLN("Max attempts reached. Entering Deep Sleep.");
    enterDeepSleep();
  }
}

void setWifiConnected(bool flag)
{
  if (flag)
  {
    wifiConnected = true;
    attemptCount = 0;
    currentDelay = INITIAL_DELAY;
    digitalWrite(LED_BUILTIN, LOW);
  }
  else
  {
    wifiConnected = false;
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

// Обработчик событий Wi-Fi
void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    DEBUG_PRINT("Got IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
    setWifiConnected(true);
    // sendDataAndSleep();            // Вызываем функцию для отправки данных и перехода в сон
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    DEBUG_PRINTLN("Wi-Fi disconnected. Reconnecting...");
    setWifiConnected(false);
    handleReconnect(); // Обработка повторного подключения
    break;
  default:
    break;
  }
}

void connectToWiFi(const char *ssid, const char *pwd)
{
  DEBUG_PRINTLN("Connecting to WiFi network: " + String(ssid));

  // delete old config
  WiFi.disconnect(true);
  // register event handler
  WiFi.onEvent(WiFiEvent); // Will call WiFiEvent() from another thread.

  // Initiate connection
  WiFi.begin(ssid, pwd);

  DEBUG_PRINTLN("Waiting for WIFI connection...");
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
#endif

  pinMode(LED_BUILTIN, OUTPUT);

  // Инициализация I2C
  Wire.begin(BME_SDA, BME_SCL);

  // Проверка подключения BME280
  if (!bme.begin(BME_ADDR))
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  DEBUG_PRINTLN("BME280 sensor connected!");

  setWifiConnected(false);
  connectToWiFi(ssid, password);
}

void loop()
{
  sendDataAndSleep(); // TODO: удали потому что если в deepsleep до лупа вообще не дойдет по идее и раскоментируй сверху
}