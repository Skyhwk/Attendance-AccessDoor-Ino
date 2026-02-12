#pragma once
#include <Arduino.h>

enum DeviceModeData
{
    MODE_ACCESS_DOOR = 0,
    MODE_ATTENDANCE = 1
};

enum DeviceMode
{
    MODE_NORMAL = 0, // Access Door
    MODE_OPEN = 1,   // Access Door
    MODE_CLOSE = 3,  // Access Door
    MODE_SCAN = 4,   // Attendance
    MODE_ADD = 5     // Attendance
};

struct DeviceConfig
{
    char ssid[32];     // default "INTILAB"
    char password[32]; // default ""

    bool dhcp;        // default true
    char ip[16];      // empty if dhcp
    char gateway[16]; // empty if dhcp
    char subnet[16];  // empty if dhcp

    char host[64];
    int port;

    char iddev[32];

    DeviceModeData modeDeviceData; // Access Door / Attendance
    DeviceMode mode;               // Normal / Scan

    int offsetday; // default 6

    char topic_subscribe[64];
    char topic_publish[64];
};

class ConfigManager
{
public:
    bool load();
    bool save();

    DeviceConfig &get();
    void setDefaultIfInvalid();

private:
    DeviceConfig config;
};

extern ConfigManager Config;
