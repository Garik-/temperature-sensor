
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>
#include <constants.h>
#include <esp32/rom/crc.h>

#include "SensorData.h"
#include "BME280Handler.h"
#include "UDPBroadcast.h"
#include "debug.h"

constexpr uint8_t LED_STATUS = 8; // Internal LED pin is 8 as per schematic

constexpr uint8_t BME_SDA = 5;     // GPIO 5 (SDA)
constexpr uint8_t BME_SCL = 6;     // GPIO 6 (SCL)
constexpr uint8_t BME_ADDR = 0x76; // Адрес BME280 по умолчанию 0x76 (может быть 0x77)
constexpr uint8_t BME_PWR = 4;     // GPIO 4 (HIGH)

constexpr uint8_t BAT_ACC = 3;

constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL; /* Conversion factor for micro seconds to seconds */
constexpr uint32_t TIME_TO_SLEEP = 60;          /* Time ESP32 will go to sleep (in seconds) */

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

RTC_DATA_ATTR static uint8_t lastChannel = 0;
RTC_DATA_ATTR static uint8_t lastBSSID[6] = {0};

class WiFiHandler
{
  static constexpr uint32_t WAIT_STATUS_TIMEOUT = 5000UL; /* Timeout for waiting for status WiFi connection */

  inline void setWifiConnected(bool flag)
  {
    if (flag)
    {
      pinMode(LED_STATUS, OUTPUT);
      digitalWrite(LED_STATUS, LOW);
    }
    else
    {
      digitalWrite(LED_STATUS, HIGH);
      pinMode(LED_STATUS, INPUT);
    }
  }

public:
  [[nodiscard]] bool begin(const char *ssid, const char *password)
  {
    if (!ssid || !password)
    {
      DEBUG_PRINTLN("Invalid WiFi credentials");
      return false;
    }

    DEBUG_PRINTF("Connecting to WiFi network: %s\n", ssid);

    setWifiConnected(false);

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_11dBm);

    const IPAddress local_IP(192, 168, 1, 11);
    const IPAddress gateway(192, 168, 1, 1);
    const IPAddress subnet(255, 255, 255, 0);

    if (!WiFi.config(local_IP, gateway, subnet))
    {
      DEBUG_PRINTLN("WiFi configuration failed");
      return false;
    }

    if (lastChannel > 0) // TODO: если перезагрузить роутер надо устройство рестартовать
    {
      WiFi.begin(ssid, password, lastChannel, lastBSSID);
    }
    else
    {
      WiFi.begin(ssid, password);
    }

    DEBUG_PRINTLN("Waiting for WIFI connection...");

    if (WiFi.waitForConnectResult(WAIT_STATUS_TIMEOUT) != WL_CONNECTED)
    {
      DEBUG_PRINTF("Connection failed with status: %d\n", WiFi.status());
      return false;
    }

    DEBUG_PRINTF("Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    WiFi.setSleep(true);
    setWifiConnected(true);

    lastChannel = WiFi.channel();
    memcpy(lastBSSID, WiFi.BSSID(), sizeof(lastBSSID));

    return true;
  }

  void end()
  {
    if (WiFi.disconnect(true, false, WAIT_STATUS_TIMEOUT))
    {
      DEBUG_PRINTLN("Wifi disconnected");
    }

    setWifiConnected(false);
    WiFi.mode(WIFI_OFF);
  }
};

void sendDataTask(BME280Handler &bme)
{
  constexpr uint8_t PACKETS_COUNT = 5;
  constexpr uint32_t PACKETS_INTERVAL = 500;

  UDPBroadcast broadcast;
  SensorData data{};

  for (uint8_t packetCounter = 0; packetCounter < PACKETS_COUNT;)
  {
    if (!bme.readSensor(data))
    {
      break;
    }

    if (broadcast.send(&data, sizeof(data)))
    {
      packetCounter++;
      if (packetCounter < PACKETS_COUNT)
      {
        delay(PACKETS_INTERVAL);
      }
    }
  }

  broadcast.flush();
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
  while (!Serial)
    delay(10);
#endif

  BME280Handler bme(BME_PWR, BME_SDA, BME_SCL, BME_ADDR);

  if (!bme.begin())
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  SensorData data{};

  int64_t duration = 0;

  for (uint8_t i = 0; i < 10; i++)
  {
    int64_t start = esp_timer_get_time();

    if (!bme.readSensor(data))
    {
      DEBUG_PRINTLN("Failed to read sensor data");
      break;
    }

    duration += esp_timer_get_time() - start;

    delay(50);
  }

  DEBUG_PRINTF("Operation took %lld µs\n", duration / 10);

  bme.end();
}

/*
void setup()
{
#if DEBUG
  Serial.begin(115200);
  while (!Serial)
    delay(10);
#endif

  btStop();
  // setCpuFrequencyMhz(120);

  BME280Handler bme(BME_PWR, BME_SDA, BME_SCL, BME_ADDR);

  if (!bme.begin())
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  DEBUG_PRINTLN("BME280 sensor connected!");

  pinMode(LED_STATUS, OUTPUT);

  if (connectToWiFi(ssid, password, WAIT_STATUS_TIMEOUT))
  {
    sendDataTask(bme);
    bme.end();

    if (WiFi.disconnect(true, false, WAIT_STATUS_TIMEOUT))
    {
      DEBUG_PRINTLN("Wifi disconnected");
    }

    setWifiConnected(false);
  }
  else
  {
    bme.end();
    DEBUG_PRINTLN("WiFi connection failed");
  }

  pinMode(LED_STATUS, INPUT);

  enterDeepSleep();
}
  */

void loop()
{
}

/*
uint32_t readBatteryVoltage() {
  constexpr uint8_t SAMPLES = 5;
  constexpr uint8_t DELAY_MS = 1;

  uint32_t sum = 0;
  for(uint8_t i = 0; i < SAMPLES; i++) {
      sum += analogRead(BAT_ACC);
      if(i < SAMPLES - 1) delay(DELAY_MS);
  }

  uint32_t average = sum / SAMPLES;
  // Пересчет с учетом делителя (1M + 1.5M)
  // ADC = 12 bit (0-4095)
  // Опорное напряжение 3.3V
  // Коэффициент делителя = (1 + 1.5) = 2.5
  return (average * 3300UL * 25UL) / (4095UL * 10UL); // результат в милливольтах
}*/