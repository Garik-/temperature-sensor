#pragma once

#ifndef BME280_HANDLER_H
#define BME280_HANDLER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "SensorData.h"

class BME280Handler
{
    static constexpr uint8_t WARMUP_MS = 2;

    const uint8_t powerPin;
    const uint8_t sdaPin;
    const uint8_t sclPin;
    const uint8_t i2cAddr;

    Adafruit_BME280 bme{};

public:
    BME280Handler(uint8_t power_pin, uint8_t sda_pin, uint8_t scl_pin, uint8_t addr);
    bool begin();
    [[nodiscard]] bool readSensor(SensorData &data);
    void end();
};

#endif