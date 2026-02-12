#pragma once
#include <Arduino.h>
#include <ESP32Time.h>

class TimeGlobal
{
public:
    void begin();
    void update(); // panggil berkala (task)

    String time();     // HH:MM:SS
    String date();     // YYYY-MM-DD
    String datetime(); // YYYY-MM-DD HH:MM:SS

    bool isSynced();

private:
    void syncNTP();

    ESP32Time _rtc;
    bool _synced = false;
    bool _syncing = false;
};

extern TimeGlobal Time;
