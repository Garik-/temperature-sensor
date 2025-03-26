
#include "BME280Handler.h"
#include "debug.h"

BME280Handler::BME280Handler(uint8_t power_pin, uint8_t sda_pin, uint8_t scl_pin, uint8_t addr)
    : powerPin(power_pin), sdaPin(sda_pin), sclPin(scl_pin), i2cAddr(addr)
{
    pinMode(powerPin, OUTPUT);
    digitalWrite(powerPin, LOW);
}

bool BME280Handler::begin()
{
    digitalWrite(powerPin, HIGH);
    delay(WARMUP_MS);

    if (!(Wire.begin(sdaPin, sclPin) && bme.begin(i2cAddr)))
    {
        DEBUG_PRINTLN("BME280 init failed!");
        end();
        return false;
    }

    /*
    X1  3612 µs
    X2  3643 µs
    */

    bme.setSampling(
        Adafruit_BME280::MODE_NORMAL,
        Adafruit_BME280::SAMPLING_X4,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X4,
        Adafruit_BME280::FILTER_OFF,
        Adafruit_BME280::STANDBY_MS_0_5);

    return true;
}

[[nodiscard]] bool BME280Handler::readSensor(SensorData &data)
{
    data.temperature = bme.readTemperature();
    data.humidity = bme.readHumidity();
    data.pressure = bme.readPressure();

#if DEBUG
    DEBUG_PRINTF(" temperature=%f humidity=%f pressure=%f\n", data.temperature, data.humidity, data.pressure);
#endif

    return data.isValid();
}

void BME280Handler::end()
{
    digitalWrite(powerPin, LOW);
    Wire.end();
    pinMode(powerPin, INPUT);
    pinMode(sdaPin, INPUT);
    pinMode(sclPin, INPUT);
}