#pragma once

#ifndef UDP_BROADCAST_H
#define UDP_BROADCAST_H

#include <WiFi.h>
#include <WiFiUDP.h>

constexpr const uint16_t UDP_PORT = 12345; // Порт UDP-сервера

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
    [[nodiscard]] bool send(const void *buffer, size_t size);

    void flush();
};

#endif