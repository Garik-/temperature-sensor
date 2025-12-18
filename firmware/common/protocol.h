#pragma once

#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

typedef struct
{
    int16_t temperature; // 23.45°C → 2345
    uint16_t humidity;   // 56.78% → 5678
    uint8_t pressure[3]; // 101325 Pa
} __attribute__((packed)) test_espnow_payload_t;

typedef struct
{
    test_espnow_payload_t payload;
    uint16_t crc; // CRC16 value of ESPNOW data.
} __attribute__((packed)) test_espnow_data_t;

void pack_pressure(uint32_t pressure, uint8_t *buf)
{
    buf[0] = (pressure >> 16) & 0xFF;
    buf[1] = (pressure >> 8) & 0xFF;
    buf[2] = pressure & 0xFF;
}

// Распаковка 3 байт в uint32_t
uint32_t unpack_pressure(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

#endif