#pragma once
#include <Arduino.h>

class DoorManager
{
public:
    void begin(int relayPin);
    void update();

    void open();
    void noTouchOpen();
    void normal();
    void forceOpen();
    void forceClose();

    int getMode() const;

private:
    enum DoorMode
    {
        DOOR_NORMAL = 0,
        DOOR_FORCE_OPEN = 1,
        DOOR_FORCE_CLOSE = 2
    };

    enum PulsePhase
    {
        PULSE_IDLE = 0,
        PULSE_PRE_DELAY = 1,
        PULSE_OPEN = 2
    };

    void cancelPulse();

    int _relayPin = -1;
    uint32_t _openMs = 3000;
    DoorMode _mode = DOOR_NORMAL;
    PulsePhase _pulse = PULSE_IDLE;
    unsigned long _phaseStartMs = 0;
};

extern DoorManager Door;
