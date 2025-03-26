
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

constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL;   /* Conversion factor for micro seconds to seconds */
constexpr uint32_t TIME_TO_SLEEP = 60;            /* Time ESP32 will go to sleep (in seconds) */
constexpr uint32_t WAIT_STATUS_TIMEOUT = 60000UL; /* Timeout for waiting for status WiFi connection */

#pragma pack(push, 1)
// Структура пакета для отправки
struct NetworkPacket
{
  static constexpr uint8_t MAGIC_START = 0xAA; // Маркер начала
  static constexpr uint8_t MAGIC_END = 0x55;   // Маркер конца
  static constexpr uint16_t VERSION = 1;       // Версия протокола

  // Заголовок пакета
  struct Header
  {
    uint8_t startMarker = MAGIC_START; // Начало пакета
    uint16_t version = VERSION;        // Версия
    uint16_t dataSize = 0;             // Размер данных
                                       // uint32_t sequence = 0;             // Номер пакета
                                       // uint8_t dataType = 0;              // Тип данных
                                       // uint8_t flags = 0;                 // Флаги
  } header;

  // Данные
  SensorData data;

  // CRC
  uint16_t crc = 0;
  uint8_t endMarker = MAGIC_END;

  // Вычисление CRC16
  [[nodiscard]] uint16_t calculateCRC() const
  {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(this);
    return crc16_le(0, bytes, sizeof(Header) + sizeof(SensorData));
  }

  // Подготовка пакета
  void prepare(const SensorData &sensorData)
  {
    header.dataSize = sizeof(SensorData);
    data = sensorData;
    crc = calculateCRC();
  }

  // Проверка целостности
  [[nodiscard]] bool isValid() const
  {
    return header.startMarker == MAGIC_START &&
           endMarker == MAGIC_END &&
           header.version == VERSION &&
           crc == calculateCRC();
  }
};
#pragma pack(pop)

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

inline void setWifiConnected(bool flag)
{
  digitalWrite(LED_STATUS, !flag);
}

[[nodiscard]] bool connectToWiFi(const char *ssid, const char *password, unsigned long timeoutLength)
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

void sendDataTask(BME280Handler &bme)
{
  constexpr uint8_t PACKETS_COUNT = 5;
  constexpr uint32_t PACKETS_INTERVAL = 500;

  UDPBroadcast broadcast;
  SensorData data{};
  NetworkPacket packet;

  for (uint8_t packetCounter = 0; packetCounter < PACKETS_COUNT;)
  {
    if (!bme.readSensor(data))
    {
      break;
    }

    packet.prepare(data);

    if (broadcast.send(&packet, sizeof(packet)))
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