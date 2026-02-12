#include "lcd_manager.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "time_global.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
LCDManager LCD;

void LCDManager::begin()
{
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.display();
}

void LCDManager::setTime(const String &timeStr)
{
    _time = timeStr;
}

void LCDManager::setStatus(bool online)
{
    _online = online;
}

void LCDManager::setInfo1(const String &text)
{
    _info1 = text;
    _blank = false;
}

void LCDManager::setInfo2(const String &text)
{
    _info2 = text;
    _blank = false;
}

void LCDManager::setDeviceName(const String &name)
{
    _deviceName = name;
}

void LCDManager::setIP(const String &ip)
{
    _ip = ip;
    _forceShowIP = false;
}

void LCDManager::setStaticIp(const String &ip)
{
    _ip = ip;
    _forceShowIP = true;
}

void LCDManager::showTemp(const String &l1, const String &l2, unsigned long duration)
{
    showTemp(l1, l2, duration, false);
}

void LCDManager::showTemp(const String &l1, const String &l2, unsigned long duration, bool clearAfter)
{
    _temp1 = l1;
    _temp2 = l2;
    _tempDuration = duration;
    _tempStart = millis();
    _tempActive = true;
    _clearAfterTemp = clearAfter;
    _blank = false;
}

void LCDManager::update()
{
    unsigned long now = millis();

    // redraw tiap 200ms
    if (now - _lastDraw < 200)
        return;

    _lastDraw = now;

    // handle temp message timeout
    if (_tempActive && (now - _tempStart >= _tempDuration))
    {
        _tempActive = false;
        if (_clearAfterTemp)
        {
            _blank = true;
            _clearAfterTemp = false;
        }
    }

    updateRotation();
    draw();
}

void LCDManager::updateRotation()
{
    unsigned long now = millis();

    if (_forceShowIP)
    {
        _showIP = true;
        return;
    }

    if (now - _rotationStart >= 10000)
    {
        _rotationStart = now;
    }

    unsigned long elapsed = now - _rotationStart;

    _showIP = (elapsed >= 8000); // 8 detik name, 2 detik IP
}

uint8_t LCDManager::getBestTextSize(String text, uint8_t maxSize)
{
    int16_t x1, y1;
    uint16_t w, h;

    for (int size = maxSize; size >= 1; size--)
    {
        display.setTextSize(size);
        display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

        if (w <= SCREEN_WIDTH)
            return size;
    }

    return 1;
}

String LCDManager::clampTextToWidth(const String &text, uint8_t textSize)
{
    int16_t x1, y1;
    uint16_t w, h;

    display.setTextSize(textSize);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if (w <= SCREEN_WIDTH)
        return text;

    String out = text;
    const String ellipsis = "...";

    while (out.length() > 0)
    {
        String candidate = out + ellipsis;
        display.getTextBounds(candidate, 0, 0, &x1, &y1, &w, &h);
        if (w <= SCREEN_WIDTH)
            return candidate;
        out.remove(out.length() - 1);
    }

    return "";
}

void LCDManager::draw()
{
    display.clearDisplay();

    // ================= HEADER =================
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print(Time.time());

    String statusText = _online ? "Online" : "Offline";

    int16_t x1, y1;
    uint16_t w, h;

    display.getTextBounds(statusText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(SCREEN_WIDTH - w, 0);
    display.print(statusText);

    // ================= BODY =================
    String line1 = _tempActive ? _temp1 : (_blank ? "" : _info1);
    String line2 = _tempActive ? _temp2 : (_blank ? "" : _info2);

    uint8_t usedSize1 = 0;

    if (line1.length() > 0)
    {
        // Line 1 fixed size + left aligned
        uint8_t size1 = 2;
        if (clampTextToWidth(line1, size1) != line1)
            size1 = 1;
        line1 = clampTextToWidth(line1, size1);
        display.setTextSize(size1);
        usedSize1 = size1;
        int16_t y1 = 18;
        display.setCursor(0, y1);
        display.print(line1);
    }

    if (line2.length() > 0)
    {
        // Line 2 fixed size + left aligned
        uint8_t size2 = 1;
        line2 = clampTextToWidth(line2, size2);
        display.setTextSize(size2);
        int16_t y2 = 38;
        if (usedSize1 > 0)
            y2 = 18 + (usedSize1 * 8) + 4;
        display.setCursor(0, y2);
        display.print(line2);
    }

    // ================= FOOTER =================
    display.setTextSize(1);
    display.setCursor(0, 56);

    if (_showIP)
        display.print(_ip);
    else
        display.print(_deviceName);

    display.display();
}
