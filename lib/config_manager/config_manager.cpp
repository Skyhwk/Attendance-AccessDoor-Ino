#include "config_manager.h"
#include <SD.h>

ConfigManager Config;

bool ConfigManager::load()
{
    File f = SD.open("/config.bin");
    if (!f)
        return false;

    memset(&config, 0, sizeof(DeviceConfig));

    size_t sz = f.size();
    size_t toRead = sz;
    if (toRead > sizeof(DeviceConfig))
        toRead = sizeof(DeviceConfig);

    f.read((uint8_t *)&config, toRead);
    f.close();

    // migration heuristic: older firmware version may have stored iddev where topic_subscribe is now
    if (config.iddev[0] == '\0' && config.topic_subscribe[0] != '\0')
    {
        String ts = String(config.topic_subscribe);
        if (ts.length() > 0 && ts.length() < (int)sizeof(config.iddev) && ts.indexOf('/') < 0)
        {
            strlcpy(config.iddev, ts.c_str(), sizeof(config.iddev));
            strcpy(config.topic_subscribe, "");
        }
    }

    return true;
}

bool ConfigManager::save()
{
    SD.remove("/config.bin");
    File f = SD.open("/config.bin", FILE_WRITE);
    if (!f)
        return false;

    f.write((uint8_t *)&config, sizeof(DeviceConfig));
    f.close();
    return true;
}

DeviceConfig &ConfigManager::get()
{
    return config;
}

void ConfigManager::setDefaultIfInvalid()
{
    // SSID
    if (config.ssid[0] == '\0')
        strcpy(config.ssid, "INTILAB");

    // Password boleh kosong
    if (config.password[0] == '\0')
        strcpy(config.password, "");

    // DHCP
    if (config.dhcp)
    {
        strcpy(config.ip, "");
        strcpy(config.gateway, "");
        strcpy(config.subnet, "");
    }

    // Port
    if (config.port <= 0)
        config.port = 1111;

    // MQTT topics boleh kosong
    if (config.topic_subscribe[0] == '\0')
        strcpy(config.topic_subscribe, "");
    if (config.topic_publish[0] == '\0')
        strcpy(config.topic_publish, "");

    // Offset day
    if (config.offsetday <= 0)
        config.offsetday = 7;

    // Mode logic
    if (config.modeDeviceData == MODE_ACCESS_DOOR)
        config.mode = MODE_NORMAL;
    else
        config.mode = MODE_SCAN;
}
