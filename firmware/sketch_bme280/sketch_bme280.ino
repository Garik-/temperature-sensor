#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>
#include <constants.h>

#ifndef SECRETS_H
const char *ssid = "";                    // Замените на ваш SSID
const char *password = "";                // Замените на ваш пароль
const char *udpAddress = "192.168.1.255"; // Широковещательный адрес
const int udpPort = 12345;                // Порт UDP-сервера
#endif

constexpr uint8_t LED_STATUS = 8;  // Internal LED pin is 8 as per schematic
constexpr uint8_t BME_SDA = 5;     // GPIO 5 (SDA)
constexpr uint8_t BME_SCL = 6;     // GPIO 6 (SCL)
constexpr uint8_t BME_ADDR = 0x76; // Адрес BME280 по умолчанию 0x76 (может быть 0x77)

constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL;   /* Conversion factor for micro seconds to seconds */
constexpr uint32_t TIME_TO_SLEEP = 60;            /* Time ESP32 will go to sleep (in seconds) */
constexpr uint32_t WAIT_STATUS_TIMEOUT = 60000UL; /* Timeout for waiting for status WiFi connection */

constexpr uint8_t PACKETS_COUNT = 5;
constexpr uint32_t PACKETS_INTERVAL = 500;

constexpr size_t MAX_UDP_PACKET_SIZE = 65507;

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
  float temperature{};
  float humidity{};
  float pressure{};

  [[nodiscard]] bool isValid() const noexcept
  {
    return !std::isnan(temperature) &&
           !std::isnan(humidity) &&
           !std::isnan(pressure);
  }
};
#pragma pack(pop)

WiFiUDP udp;
Adafruit_BME280 bme;

void enterDeepSleep() noexcept
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

/**
 * @brief Sends data over UDP
 * @param buffer Pointer to the data buffer
 * @param size Size of data in bytes
 * @return true if sending was successful, false otherwise
 */
[[nodiscard]] inline bool sendData(const void *buffer, size_t size) noexcept
{
  if (buffer == nullptr)
  {
    return false;
  }

  if (size == 0 || size > MAX_UDP_PACKET_SIZE)
  {
    return false;
  }

  if (!udp.beginPacket(udpAddress, udpPort))
  {
    return false;
  }

  udp.write(static_cast<const uint8_t *>(buffer), size);

  return udp.endPacket();
}

inline bool readSensor(SensorData &data) noexcept
{
  data.temperature = bme.readTemperature();
  data.humidity = bme.readHumidity();
  data.pressure = bme.readPressure();

#if DEBUG
  DEBUG_PRINTF(" temperature=%f humidity=%f pressure=%f\n", data.temperature, data.humidity, data.pressure);
#endif

  return data.isValid();
}

void sendDataTask() noexcept
{
  SensorData data{};
  for (uint8_t packetCounter = 0; packetCounter < PACKETS_COUNT;)
  {
    if (!readSensor(data))
    {
      delay(100);
      continue;
    }

    if (sendData(&data, sizeof(data)))
    {
      packetCounter++;
      if (packetCounter < PACKETS_COUNT)
      {
        delay(PACKETS_INTERVAL);
      }
    }
  }

  udp.flush();
}

inline void setWifiConnected(bool flag) noexcept
{
  digitalWrite(LED_STATUS, !flag);
}

[[nodiscard]] bool connectToWiFi(const char *ssid, const char *password, unsigned long timeoutLength) noexcept
{
  if (!ssid || !password)
  {
    DEBUG_PRINTLN("Invalid WiFi credentials");
    return false;
  }

  DEBUG_PRINTF("Connecting to WiFi network: %s\n", ssid);

  setWifiConnected(false);

  WiFi.mode(WIFI_STA);

  const IPAddress local_IP(192, 168, 1, 11);
  const IPAddress gateway(192, 168, 1, 1);
  const IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.config(local_IP, gateway, subnet))
  {
    DEBUG_PRINTLN("WiFi configuration failed");
    return false;
  }

  WiFi.begin(ssid, password);

  if (const int8_t rssi = WiFi.RSSI(); rssi != 0)
  {
    DEBUG_PRINTF("RSSI: %d\n", rssi);
  }

  DEBUG_PRINTLN("Waiting for WIFI connection...");

  if (WiFi.waitForConnectResult(timeoutLength) != WL_CONNECTED)
  {
    DEBUG_PRINTF("Connection failed with status: %d\n", WiFi.status());
    return false;
  }

  DEBUG_PRINTF("Connected, IP: %s\n", WiFi.localIP().toString().c_str());

  setWifiConnected(true);

  return true;
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
  while (!Serial)
    delay(10);
#endif

  Wire.begin(BME_SDA, BME_SCL);
  if (!bme.begin(BME_ADDR))
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  DEBUG_PRINTLN("BME280 sensor connected!");

  pinMode(LED_STATUS, OUTPUT);

  if (connectToWiFi(ssid, password, WAIT_STATUS_TIMEOUT))
  {
    sendDataTask();

    if (WiFi.disconnect(true, false, WAIT_STATUS_TIMEOUT))
    {
      DEBUG_PRINTLN("Wifi disconnected");
    }

    setWifiConnected(false);
  }
  else
  {
    DEBUG_PRINTLN("WiFi connection failed");
  }

  pinMode(LED_STATUS, INPUT);
  enterDeepSleep();
}

void loop()
{
}