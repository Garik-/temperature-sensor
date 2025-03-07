#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>

#define LED_BUILTIN 8 // Internal LED pin is 8 as per schematic

#define BME_SDA 5     // GPIO 5 (SDA)
#define BME_SCL 6     // GPIO 6 (SCL)
#define BME_ADDR 0x76 // Адрес BME280 по умолчанию 0x76 (может быть 0x77)

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 60           /* Time ESP32 will go to sleep (in seconds) */

#define PACKETS_COUNT 5
#define PACKETS_INTERVAL 500

#define WIFI_CONNECTED(x) digitalWrite(LED_BUILTIN, !x);

#define DEBUG 1
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

WiFiUDP udp;
Adafruit_BME280 bme;

void enterDeepSleep()
{
  if (TIME_TO_SLEEP == 0)
  {
    return;
  }

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  DEBUG_PRINTLN("Entering Deep Sleep for " + String(TIME_TO_SLEEP) + " seconds");

#ifdef DEBUG
  Serial.flush();
#endif
  esp_deep_sleep_start();
}

// Функция для отправки данных и перехода в сон
bool sendData()
{
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
  while (packetCounter < PACKETS_COUNT)
  {
    if (sendData())
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
  DEBUG_PRINTLN("Connecting to WiFi network: " + String(ssid));

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

  if (WiFi.waitForConnectResult() != WL_CONNECTED)
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

  while (!WiFi.disconnect(true))
  {
    delay(200);
  }

  DEBUG_PRINTLN("Wifi disconnected");

  setWifiConnected(false);
  pinMode(LED_BUILTIN, INPUT);

  enterDeepSleep();
}

void loop()
{
}