#include "time_global.h"
#include <WiFi.h>
#include "config_manager.h"

TimeGlobal Time;

void TimeGlobal::begin()
{
    _rtc.setTime(0); // start dari 00:00:00
}

void TimeGlobal::update()
{
    if (WiFi.isConnected() && !_synced && !_syncing)
    {
        syncNTP();
    }
}

void TimeGlobal::syncNTP()
{
    _syncing = true;

    auto &cfg = Config.get();

    long gmtOffset = cfg.offsetday * 3600; // offset dalam jam
    long daylightOffset = 0;

    configTime(gmtOffset, daylightOffset, "pool.ntp.org");

    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        _rtc.setTimeStruct(timeinfo);
        _synced = true;
    }

    _syncing = false;
}

String TimeGlobal::time()
{
    return _rtc.getTime("%H:%M:%S");
}

String TimeGlobal::date()
{
    return _rtc.getTime("%Y-%m-%d");
}

String TimeGlobal::datetime()
{
    return _rtc.getTime("%Y-%m-%dT%H:%M:%S");
}

bool TimeGlobal::isSynced()
{
    return _synced;
}
