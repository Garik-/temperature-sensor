#pragma once
#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#pragma pack(push, 1)
struct SensorData
{
    float temperature{};
    float humidity{};
    float pressure{};
    float voltage{};

    [[nodiscard]] bool isValid() const
    {
        return (temperature == temperature) &&
               (humidity == humidity) &&
               (pressure == pressure) &&
               (voltage == voltage);
    }
};
#pragma pack(pop)

#endif