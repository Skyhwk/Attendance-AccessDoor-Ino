#include "buzzer_manager.h"

BuzzerManager Buzzer;

void BuzzerManager::begin(uint8_t pin)
{
    _pin = pin;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void BuzzerManager::play(uint8_t count, uint16_t onTime, uint16_t offTime)
{
    _onTime = onTime;
    _offTime = offTime;
    _active = true;

    // Start immediately with ON
    digitalWrite(_pin, HIGH);
    _state = true;
    _lastChange = millis();

    // We have already started the first ON phase, so remaining phases are:
    // for N beeps: (ON,OFF) repeated N times -> 2N phases total
    // after starting ON, remaining phases = 2N - 1
    _remaining = (int)count * 2 - 1;
}

void BuzzerManager::update()
{
    if (!_active)
        return;

    unsigned long now = millis();

    if (_state) // sedang ON
    {
        if (now - _lastChange >= _onTime)
        {
            digitalWrite(_pin, LOW);
            _state = false;
            _lastChange = now;
            _remaining--;
            if (_remaining <= 0)
            {
                _active = false;
                return;
            }
        }
    }
    else // sedang OFF
    {
        if (now - _lastChange >= _offTime)
        {
            digitalWrite(_pin, HIGH);
            _state = true;
            _lastChange = now;
            _remaining--;
            if (_remaining <= 0)
            {
                _active = false;
                return;
            }
        }
    }
}

void BuzzerManager::granted()
{
    play(2, 80, 100); // 2x agak panjang
}

void BuzzerManager::reject()
{
    play(3, 80, 100); // 3x sedang
}

void BuzzerManager::reboot()
{
    play(3, 200, 200); // 3x sedang
}

void BuzzerManager::reset()
{
    play(10, 80, 100); // 5x pendek
}

void BuzzerManager::found()
{
    play(1, 100, 100); // 1x pendek
}
