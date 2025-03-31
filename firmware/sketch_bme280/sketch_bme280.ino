#include "debug.h"
#include "secrets.h"
#include "SensorData.h"
#include "BME280Handler.h"
#include "UDPBroadcast.h"
#include "WiFiHandler.h"
#include "CriticalError.h"

constexpr uint8_t BME_SDA = 5;     // GPIO 5 (SDA)
constexpr uint8_t BME_SCL = 6;     // GPIO 6 (SCL)
constexpr uint8_t BME_ADDR = 0x76; // Адрес BME280 по умолчанию 0x76 (может быть 0x77)
constexpr uint8_t BME_PWR = 4;     // GPIO 4 (HIGH)

constexpr uint8_t BAT_ACC = 3;
constexpr float VOLTAGE_DIVIDER = 1.667; // 1M + 1.5M

constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL; /* Conversion factor for micro seconds to seconds */
constexpr uint32_t TIME_TO_SLEEP = 60;          /* Time ESP32 will go to sleep (in seconds) */

inline void enterDeepSleep()
{
  if (TIME_TO_SLEEP == 0)
  {
    return;
  }

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void sendDataTask(BME280Handler &bme)
{
  constexpr uint8_t PACKETS_COUNT = 5;
  constexpr uint32_t PACKETS_INTERVAL = 50;

  UDPBroadcast broadcast;
  SensorData data{};

  uint32_t analogVolts = 0;

  for (uint8_t packetCounter = 0; packetCounter < PACKETS_COUNT;)
  {
    if (!bme.readSensor(data))
    {
      break;
    }

    analogVolts += analogReadMilliVolts(BAT_ACC);
    data.voltage = (analogVolts / (packetCounter + 1)) * VOLTAGE_DIVIDER;

#if DEBUG
    DEBUG_PRINTF(" temperature=%f humidity=%f pressure=%f voltage=%f\n", data.temperature, data.humidity, data.pressure, data.voltage);
#endif

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
  int64_t start = esp_timer_get_time();
  btStop();

#if DEBUG
  Serial.begin(115200);
  while (!Serial)
    delay(10);
#endif

  pinMode(BME_PWR, OUTPUT);
  digitalWrite(BME_PWR, HIGH);
  delay(2);

  handleCriticalError(CriticalError::BME280_Init_Failed);
  return;

  BME280Handler bme(BME_PWR, BME_SDA, BME_SCL, BME_ADDR);

  if (bme.begin())
  {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    WiFiHandler wifi;

    if (wifi.begin(ssid, password))
    {
      sendDataTask(bme);
    }
    else
    {
      DEBUG_PRINTLN("WiFi connection failed");
      handleCriticalError(CriticalError::WiFi_Connection_Failed);
    }

    wifi.end();
    bme.end();
  }
  else
  {
    DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
    handleCriticalError(CriticalError::BME280_Init_Failed);
  }

  DEBUG_PRINTF("Operation took %lld µs\n", esp_timer_get_time() - start);
  DEBUG_PRINTF("Entering Deep Sleep for %d seconds\n", TIME_TO_SLEEP);

#ifdef DEBUG
  Serial.flush();
  Serial.end();
#endif

  enterDeepSleep();
}

void loop()
{
}