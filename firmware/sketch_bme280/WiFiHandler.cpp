#include "WiFiHandler.h"

inline void WiFiHandler::setWifiConnected(bool flag)
{
    if (flag)
    {
        pinMode(LED_STATUS, OUTPUT);
        digitalWrite(LED_STATUS, LOW);
    }
    else
    {
        digitalWrite(LED_STATUS, HIGH);
        pinMode(LED_STATUS, INPUT);
    }
}

[[nodiscard]] bool WiFiHandler::begin(const char *ssid, const char *password)
{
    if (!ssid || !password)
    {
        DEBUG_PRINTLN("Invalid WiFi credentials");
        return false;
    }

    DEBUG_PRINTF("Connecting to WiFi network: %s\n", ssid);

    setWifiConnected(false);

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_11dBm);

    const IPAddress local_IP(192, 168, 1, 11);
    const IPAddress gateway(192, 168, 1, 1);
    const IPAddress subnet(255, 255, 255, 0);

    if (!WiFi.config(local_IP, gateway, subnet))
    {
        DEBUG_PRINTLN("WiFi configuration failed");
        return false;
    }

    if (WiFi.channel() > 0) // TODO: если перезагрузить роутер надо устройство рестартовать
    {
        DEBUG_PRINTLN("WIFI using stored config");
        WiFi.begin();
    }
    else
    {
        WiFi.begin(ssid, password);
    }

    DEBUG_PRINTLN("Waiting for WIFI connection...");

    if (WiFi.waitForConnectResult(WAIT_STATUS_TIMEOUT) != WL_CONNECTED)
    {
        DEBUG_PRINTF("Connection failed with status: %d\n", WiFi.status());
        return false;
    }

    DEBUG_PRINTF("Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    WiFi.setSleep(true);
    setWifiConnected(true);

    return true;
}

void WiFiHandler::end()
{
    if (WiFi.disconnect(true, false, WAIT_STATUS_TIMEOUT))
    {
        DEBUG_PRINTLN("Wifi disconnected");
    }

    setWifiConnected(false);
    WiFi.mode(WIFI_OFF);
}
