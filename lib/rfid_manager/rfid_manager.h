#pragma once
#include <Arduino.h>

class RFIDManager
{
public:
    void begin(int rxPin);
    String read();
    void loop();

private:
    bool readTag(String &outTag);
    void handleTag(const String &tag);
};

extern RFIDManager RFID;
