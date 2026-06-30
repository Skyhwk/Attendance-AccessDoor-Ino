#include "door_manager.h"
#include "buzzer_manager.h"

DoorManager Door;

void DoorManager::begin(int relayPin)
{
    _relayPin = relayPin;
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, LOW);
    _mode = DOOR_NORMAL;
    _pulse = PULSE_IDLE;
}

void DoorManager::cancelPulse()
{
    _pulse = PULSE_IDLE;
    if (_mode != DOOR_FORCE_OPEN)
        digitalWrite(_relayPin, LOW);
}

void DoorManager::update()
{
    if (_relayPin < 0 || _pulse == PULSE_IDLE)
        return;

    if (_mode == DOOR_FORCE_CLOSE)
    {
        cancelPulse();
        return;
    }

    if (_mode == DOOR_FORCE_OPEN)
    {
        digitalWrite(_relayPin, HIGH);
        _pulse = PULSE_IDLE;
        return;
    }

    unsigned long elapsed = millis() - _phaseStartMs;

    if (_pulse == PULSE_PRE_DELAY)
    {
        if (elapsed >= 150)
        {
            digitalWrite(_relayPin, HIGH);
            _phaseStartMs = millis();
            _pulse = PULSE_OPEN;
        }
        return;
    }

    if (_pulse == PULSE_OPEN && elapsed >= _openMs)
    {
        digitalWrite(_relayPin, LOW);
        _pulse = PULSE_IDLE;
    }
}

void DoorManager::open()
{
    if (_relayPin < 0)
        return;

    if (_mode == DOOR_FORCE_CLOSE)
        return;

    if (_mode == DOOR_FORCE_OPEN)
    {
        digitalWrite(_relayPin, HIGH);
        return;
    }

    if (_pulse != PULSE_IDLE)
        return;

    _pulse = PULSE_PRE_DELAY;
    _phaseStartMs = millis();
}

void DoorManager::noTouchOpen()
{
    if (_relayPin < 0)
        return;

    if (_mode == DOOR_FORCE_CLOSE)
        return;

    if (_mode == DOOR_FORCE_OPEN)
    {
        digitalWrite(_relayPin, HIGH);
        return;
    }

    if (_pulse != PULSE_IDLE)
        return;

    Buzzer.found();
    _pulse = PULSE_PRE_DELAY;
    _phaseStartMs = millis();
}

void DoorManager::normal()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_NORMAL;
    cancelPulse();
}

void DoorManager::forceOpen()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_FORCE_OPEN;
    _pulse = PULSE_IDLE;
    digitalWrite(_relayPin, HIGH);
}

void DoorManager::forceClose()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_FORCE_CLOSE;
    cancelPulse();
}

int DoorManager::getMode() const
{
    return (int)_mode;
}
