#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <secrets.h>
#include <constants.h>
#include <esp32/rom/crc.h>

constexpr const uint16_t UDP_PORT = 12345; // Порт UDP-сервера

constexpr uint8_t LED_STATUS = 8;  // Internal LED pin is 8 as per schematic
constexpr uint8_t BME_SDA = 5;     // GPIO 5 (SDA)
constexpr uint8_t BME_SCL = 6;     // GPIO 6 (SCL)
constexpr uint8_t BME_ADDR = 0x76; // Адрес BME280 по умолчанию 0x76 (может быть 0x77)
constexpr uint8_t BME_PWR = 4;

constexpr uint8_t BAT_ACC = 3;

constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL;   /* Conversion factor for micro seconds to seconds */
constexpr uint32_t TIME_TO_SLEEP = 60;            /* Time ESP32 will go to sleep (in seconds) */
constexpr uint32_t WAIT_STATUS_TIMEOUT = 60000UL; /* Timeout for waiting for status WiFi connection */

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

  [[nodiscard]] bool isValid() const
  {
    return (temperature == temperature) &&
           (humidity == humidity) &&
           (pressure == pressure);
  }
};

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

class UDPBroadcast
{
  WiFiUDP udp{};
  static constexpr size_t MAX_UDP_PACKET_SIZE = 65507;

public:
  /**
   * @brief Sends data over UDP
   * @param buffer Pointer to the data buffer
   * @param size Size of data in bytes
   * @return true if sending was successful, false otherwise
   */
  [[nodiscard]] bool send(const void *buffer, size_t size)
  {
    if (buffer == nullptr)
    {
      return false;
    }

    if (size == 0 || size > MAX_UDP_PACKET_SIZE)
    {
      return false;
    }

    if (!udp.beginPacket(WiFi.broadcastIP(), UDP_PORT))
    {
      return false;
    }

    udp.write(static_cast<const uint8_t *>(buffer), size);

    return udp.endPacket() == 1;
  }

  void flush()
  {
    udp.flush();
  }
};

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

class BME280Handler
{
  static constexpr uint8_t WARMUP_MS = 2; // Время на стабилизацию питания

  const uint8_t powerPin;
  const uint8_t sdaPin;
  const uint8_t sclPin;
  const uint8_t i2cAddr;

  Adafruit_BME280 bme{};

public:
  BME280Handler(uint8_t power_pin, uint8_t sda_pin, uint8_t scl_pin, uint8_t addr)
      : powerPin(power_pin), sdaPin(sda_pin), sclPin(scl_pin), i2cAddr(addr)
  {
    pinMode(powerPin, OUTPUT);
    digitalWrite(powerPin, LOW);
  }

  bool begin()
  {
    digitalWrite(powerPin, HIGH);
    delay(WARMUP_MS);

    if (!(Wire.begin(sdaPin, sclPin) && bme.begin(i2cAddr)))
    {
      DEBUG_PRINTLN("BME280 init failed!");
      end();
      return false;
    }

    bme.setSampling(
        Adafruit_BME280::MODE_NORMAL,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_0_5);

    return true;
  }

  [[nodiscard]] bool readSensor(SensorData &data)
  {
    data.temperature = bme.readTemperature();
    data.humidity = bme.readHumidity();
    data.pressure = bme.readPressure();

#if DEBUG
    DEBUG_PRINTF(" temperature=%f humidity=%f pressure=%f\n", data.temperature, data.humidity, data.pressure);
#endif

    return data.isValid();
  }

  void end()
  {
    digitalWrite(powerPin, LOW);
    Wire.end();
    pinMode(powerPin, INPUT);
    pinMode(sdaPin, INPUT);
    pinMode(sclPin, INPUT);
  }
};

void sendDataTask(BME280Handler &bme) // TODO: не нравится мне контроль обработки ошибок
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

void loop()
{
}