#include "door_manager.h"
#include "buzzer_manager.h"

DoorManager Door;

void DoorManager::begin(int relayPin)
{
    _relayPin = relayPin;
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, LOW);
    _mode = DOOR_NORMAL;
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

    vTaskDelay(pdMS_TO_TICKS(150));

    digitalWrite(_relayPin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(_openMs));
    digitalWrite(_relayPin, LOW);
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

    Buzzer.found();
    vTaskDelay(pdMS_TO_TICKS(150));

    digitalWrite(_relayPin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(_openMs));
    digitalWrite(_relayPin, LOW);
}

void DoorManager::normal()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_NORMAL;
    digitalWrite(_relayPin, LOW);
}

void DoorManager::forceOpen()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_FORCE_OPEN;
    digitalWrite(_relayPin, HIGH);
}

void DoorManager::forceClose()
{
    if (_relayPin < 0)
        return;
    _mode = DOOR_FORCE_CLOSE;
    digitalWrite(_relayPin, LOW);
}

int DoorManager::getMode() const
{
    return (int)_mode;
}
