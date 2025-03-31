#pragma once

#ifndef CRITICAL_ERROR_H
#define CRITICAL_ERROR_H

#include <Arduino.h>
#include "debug.h"

enum class CriticalError : uint8_t
{
    None = 0,
    BME280_Init_Failed,
    WiFi_Connection_Failed
};

class ErrorIndicator
{
private:
    static constexpr uint16_t SHORT_BLINK = 200;  // ms
    static constexpr uint16_t LONG_BLINK = 800;   // ms
    static constexpr uint16_t ERROR_PAUSE = 2000; // ms

    const uint8_t pin;
    CriticalError errorCode;
    uint32_t lastBlinkTime{};
    uint8_t blinkCount{};
    bool ledState{};

public:
    explicit ErrorIndicator(uint8_t ledPin) : pin(ledPin), errorCode(CriticalError::None)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH); // LED выключен
    }

    // Установить код ошибки для индикации
    void setError(CriticalError error)
    {
        errorCode = error;
        blinkCount = 0;
        ledState = false;
    }

    // Обновление состояния LED индикации
    void update()
    {
        if (errorCode == CriticalError::None)
        {
            return;
        }

        const uint32_t currentTime = millis();

        // Начало новой серии морганий
        if (blinkCount == 0 && (currentTime - lastBlinkTime >= ERROR_PAUSE))
        {
            blinkCount = static_cast<uint8_t>(errorCode);
            ledState = true;
            digitalWrite(pin, !ledState); // Инвертируем из-за активного низкого уровня
            lastBlinkTime = currentTime;
            return;
        }

        // В процессе индикации ошибки
        if (blinkCount > 0)
        {
            const uint16_t interval = ledState ? SHORT_BLINK : LONG_BLINK;

            if (currentTime - lastBlinkTime >= interval)
            {
                ledState = !ledState;
                digitalWrite(pin, !ledState);
                lastBlinkTime = currentTime;

                if (!ledState)
                {
                    blinkCount--;
                }
            }
        }
    }

    ~ErrorIndicator()
    {
        digitalWrite(pin, HIGH); // Выключаем LED
        pinMode(pin, INPUT);     // Освобождаем пин
    }
};

// Функция обработки критической ошибки
void handleCriticalError(CriticalError error, unsigned long timeoutLength = 30000UL) noexcept
{
    ErrorIndicator errorLed(LED_STATUS);
    errorLed.setError(error);

    DEBUG_PRINT("Critical error: ");
    switch (error)
    {
    case CriticalError::BME280_Init_Failed:
        DEBUG_PRINTLN("BME280 initialization failed!");
        break;
    case CriticalError::WiFi_Connection_Failed:
        DEBUG_PRINTLN("WiFi connection failed!");
        break;
    default:
        DEBUG_PRINTLN("Unknown error!");
        break;
    }

    unsigned long start = millis();
    while ((millis() - start) < timeoutLength)
    {
        errorLed.update();

#if DEBUG

        delay(10);
#endif
    }
}

#endif
