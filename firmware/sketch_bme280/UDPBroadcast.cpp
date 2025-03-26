#include "UDPBroadcast.h"

[[nodiscard]] bool UDPBroadcast::send(const void *buffer, size_t size)
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

void UDPBroadcast::flush()
{
    udp.flush();
}
