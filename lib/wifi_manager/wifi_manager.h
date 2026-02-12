#pragma once
#include <WiFi.h>

class WifiManager
{
public:
    bool begin();
    void handle();
    bool isConnected();
    bool justConnected();

private:
    void connect();

    bool _wasConnected = false;
    bool _justConnected = false;

    unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 10000; // 10 detik
};

extern WifiManager Wifi;