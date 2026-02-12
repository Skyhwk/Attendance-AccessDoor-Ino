#pragma once
#include <Arduino.h>

class BuzzerManager
{
public:
    void begin(uint8_t pin);
    void update();

    void granted(); // 2x agak panjang
    void reject();  // 3x sedang
    void reboot();  // 3x sedang
    void reset();   // 5x pendek
    void found();   // 1x pendek

private:
    void play(uint8_t count, uint16_t onTime, uint16_t offTime);

    uint8_t _pin;
    bool _active = false;
    bool _state = false;

    int _remaining = 0;
    uint16_t _onTime = 0;
    uint16_t _offTime = 0;

    unsigned long _lastChange = 0;
};

extern BuzzerManager Buzzer;
