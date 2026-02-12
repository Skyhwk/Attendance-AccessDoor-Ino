#pragma once
#include <Arduino.h>

class PositioningManager
{
public:
    void begin();
    void loop();

    bool isSynced() const;

private:
    bool doSync();
    bool fetchInfo(String &outName, String &outMode, String &outPath);
    bool downloadAccessBin(const String &url, uint32_t timeoutMs);

    bool _pending = true;
    bool _attempted = false;
    bool _synced = false;
};

extern PositioningManager Positioning;
