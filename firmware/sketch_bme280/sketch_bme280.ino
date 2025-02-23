#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>

/*
TODO: сейчас идеальная ситуация когда сеть доступна,
в случае если сеть не доступна надо
*/

#define LED_BUILTIN 8 // Internal LED pin is 8 as per schematic

// Настройка I2C
#define BME_SDA 5     // GPIO 5 (SDA)
#define BME_SCL 6     // GPIO 6 (SCL)
#define BME_ADDR 0x76 // Адрес BME280 по умолчанию 0x76 (может быть 0x77)

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 3           /* Time ESP32 will go to sleep (in seconds) */

// #define DEBUG 1 // 1 - включить отладку, 0 - отключить
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

#define PACKETS_COUNT 5
#define PACKETS_INTERVAL 1000

WiFiUDP udp;
Adafruit_BME280 bme;

// Флаги для управления состоянием
bool wifiConnected = false;

void _shutdown()
{

#ifdef DEBUG
  Serial.flush();
  Serial.end();
#endif

  Wire.end();
  udp.flush();
}

void enterDeepSleep()
{
  _shutdown();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  DEBUG_PRINTLN("Entering Deep Sleep for " + String(TIME_TO_SLEEP) + " seconds");
  esp_deep_sleep_start();
}

// Функция для отправки данных и перехода в сон
bool _sendData()
{
  if (!wifiConnected)
  {
    return false;
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
    return udp.endPacket();
  }

  return false;
}

void sendDataTask()
{
  int packetCounter = 0;
  while (packetCounter <= PACKETS_COUNT)
  {
    if (_sendData())
    {
      packetCounter++;
      delay(PACKETS_INTERVAL);
    }
  }
}

void setWifiConnected(bool flag)
{
  wifiConnected = flag;
  digitalWrite(LED_BUILTIN, !flag);
}

void connectToWiFi(const char *ssid, const char *pwd)
{
  DEBUG_PRINTLN("Connecting to WiFi network: " + String(ssid));

  setWifiConnected(false);
  WiFi.disconnect(true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pwd);

  DEBUG_PRINTLN("Waiting for WIFI connection...");

  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    DEBUG_PRINTLN("WiFi Failed");
    enterDeepSleep();
  }

  DEBUG_PRINT("Got IP: ");
  DEBUG_PRINTLN(WiFi.localIP());
  setWifiConnected(true);
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
#endif

  Wire.begin(BME_SDA, BME_SCL);
  if (!bme.begin(BME_ADDR))
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  DEBUG_PRINTLN("BME280 sensor connected!");

  pinMode(LED_BUILTIN, OUTPUT);
  connectToWiFi(ssid, password);

  sendDataTask();

  WiFi.disconnect(true);
  enterDeepSleep();
}

void loop()
{
}