#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>
#include <assert.h>

#ifndef SECRETS_H
const char *ssid = "";                    // Замените на ваш SSID
const char *password = "";                // Замените на ваш пароль
const char *udpAddress = "192.168.1.255"; // Широковещательный адрес
const int udpPort = 12345;                // Порт UDP-сервера
#endif

#define LED_BUILTIN 8 // Internal LED pin is 8 as per schematic

#define BME_SDA 5     // GPIO 5 (SDA)
#define BME_SCL 6     // GPIO 6 (SCL)
#define BME_ADDR 0x76 // Адрес BME280 по умолчанию 0x76 (может быть 0x77)

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 60          /* Time ESP32 will go to sleep (in seconds) */

#define PACKETS_COUNT 5
#define PACKETS_INTERVAL 500

#define WIFI_CONNECTED(x) digitalWrite(LED_BUILTIN, !x);

#define DEBUG 1
#if DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(fmt, ...)
#endif

#pragma pack(push, 1)
struct SensorData
{
  float temperature;
  float humidity;
  float pressure;
};
#pragma pack(pop)

WiFiUDP udp;
Adafruit_BME280 bme;

void enterDeepSleep()
{
  if (TIME_TO_SLEEP == 0)
  {
    return;
  }

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  DEBUG_PRINTF("Entering Deep Sleep for %d seconds\n", TIME_TO_SLEEP);

#ifdef DEBUG
  Serial.flush();
#endif
  esp_deep_sleep_start();
}

inline bool sendData(const void *buffer, size_t size)
{
  if (!buffer)
    return false;

  if (udp.beginPacket(udpAddress, udpPort))
  {
    udp.write(static_cast<const uint8_t *>(buffer), size);
    return udp.endPacket();
  }

  return false;
}

void sendDataTask()
{
  struct SensorData data;
  int packetCounter = 0;
  while (packetCounter < PACKETS_COUNT)
  {
    data.temperature = bme.readTemperature();
    data.humidity = bme.readHumidity();
    data.pressure = bme.readPressure();

#if DEBUG
    DEBUG_PRINTF(" temperature=%f humidity=%f pressure=%f\n", data.temperature, data.humidity, data.pressure);
#endif

    if (sendData(&data, sizeof(data)))
    {
      packetCounter++;
      delay(PACKETS_INTERVAL);
    }
  }

  udp.flush();
}

inline void setWifiConnected(bool flag)
{
  digitalWrite(LED_BUILTIN, !flag);
}

bool connectToWiFi(const char *ssid, const char *password)
{
  assert(ssid != nullptr);
  assert(password != nullptr);

  DEBUG_PRINTF("Connecting to WiFi network: %s\n", ssid);

  setWifiConnected(false);

  WiFi.mode(WIFI_STA);
  IPAddress local_IP(192, 168, 1, 11);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(ssid, password);

  int rssi = WiFi.RSSI();
  DEBUG_PRINT("RSSI: ");
  DEBUG_PRINTLN(rssi);

  DEBUG_PRINTLN("Waiting for WIFI connection...");

  if (WiFi.waitForConnectResult(60000UL) != WL_CONNECTED)
  {
    wl_status_t status = WiFi.status();
    DEBUG_PRINT("Connection failed with status: ");
    DEBUG_PRINTLN(status);
    return false;
  }

  DEBUG_PRINT("Got IP: ");
  DEBUG_PRINTLN(WiFi.localIP());

  setWifiConnected(true);

  return true;
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

  while (!connectToWiFi(ssid, password))
  {
    delay(200);
  }

  sendDataTask();

  if (WiFi.disconnect(true, false, 60000UL))
  {
    DEBUG_PRINTLN("Wifi disconnected");
  }

  setWifiConnected(false);
  pinMode(LED_BUILTIN, INPUT);

  enterDeepSleep();
}

void loop()
{
}