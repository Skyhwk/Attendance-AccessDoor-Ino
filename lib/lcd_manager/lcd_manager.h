#pragma once
#include <Arduino.h>

class LCDManager
{
public:
    void begin();
    void update(); // panggil berkala

    void setTime(const String &timeStr);
    void setStatus(bool online);
    void setInfo1(const String &text);
    void setInfo2(const String &text);
    void setDeviceName(const String &name);
    void setIP(const String &ip);
    void setStaticIp(const String &ip);

    void showTemp(const String &l1, const String &l2, unsigned long duration);
    void showTemp(const String &l1, const String &l2, unsigned long duration, bool clearAfter);

private:
    void draw();
    void updateRotation();
    uint8_t getBestTextSize(String text, uint8_t maxSize);
    String clampTextToWidth(const String &text, uint8_t textSize);

    // header
    String _time;
    bool _online = false;

    // default body
    String _info1;
    String _info2;

    // footer rotation
    String _deviceName;
    String _ip;
    bool _showIP = false;
    bool _forceShowIP = false;
    unsigned long _rotationStart = 0;

    // temporary message
    bool _tempActive = false;
    unsigned long _tempStart = 0;
    unsigned long _tempDuration = 0;
    String _temp1;
    String _temp2;
    bool _clearAfterTemp = false;
    bool _blank = false;

    unsigned long _lastDraw = 0;
};

extern LCDManager LCD;
