#pragma once

#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <WiFi.h>
#include "debug.h"

class WiFiHandler
{
    static constexpr uint32_t WAIT_STATUS_TIMEOUT = 5000; /* Timeout for waiting for status WiFi connection */

    inline void setWifiConnected(bool flag);

public:
    [[nodiscard]] bool begin(const char *ssid, const char *password);
    void end();
};

#endif