#pragma once

#include <Arduino.h>

class PortalConfig
{
public:
    bool isActive() const;
    bool isApMode() const;

    bool beginApOnHold(uint8_t buttonPin, uint32_t holdMs = 10000);
    bool beginLan();

    void loop();

private:
    void startAp();
    void startServer();
};

extern PortalConfig Portal;
