#pragma once

#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG 1

constexpr uint8_t LED_STATUS = 8; // Internal LED pin is 8 as per schematic

#if DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(fmt, ...)
#endif

#endif