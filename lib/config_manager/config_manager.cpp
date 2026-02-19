#include "config_manager.h"
#include <SD.h>

ConfigManager Config;

bool ConfigManager::load()
{
    File f = SD.open("/config.bin");
    if (!f)
    {
        Serial.println("[Config] config.bin not found, creating default...");
        memset(&config, 0, sizeof(DeviceConfig));
        setDefaultIfInvalid();
        if (!save())
        {
            Serial.println("[Config] Failed to create default config.bin!");
            return false;
        }
        Serial.println("[Config] Default config.bin created successfully");
        return true;
    }

    memset(&config, 0, sizeof(DeviceConfig));

    size_t sz = f.size();
    size_t toRead = sz;
    if (toRead > sizeof(DeviceConfig))
        toRead = sizeof(DeviceConfig);

    size_t read = f.read((uint8_t *)&config, toRead);
    f.close();

    Serial.println("[Config] Load filesize=" + String(sz) + " read=" + String(read) + " modeDeviceData=" + String((int)config.modeDeviceData) + " mode=" + String((int)config.mode));

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

    setDefaultIfInvalid();

    return true;
}

bool ConfigManager::save()
{
    SD.remove("/config.bin");
    File f = SD.open("/config.bin", FILE_WRITE);
    if (!f)
        return false;

    size_t written = f.write((uint8_t *)&config, sizeof(DeviceConfig));
    f.flush(); // Pastikan data benar-benar ditulis ke SD card
    f.close();

    Serial.println("[Config] Save mode=" + String((int)config.mode) + " written=" + String(written) + " expected=" + String(sizeof(DeviceConfig)));

    return (written == sizeof(DeviceConfig));
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
        strcpy(config.topic_subscribe, "/intilab/iot/multidevices");
    if (config.topic_publish[0] == '\0')
        strcpy(config.topic_publish, "/intilab/iot/log-access");

    // Offset day
    if (config.offsetday <= 0)
        config.offsetday = 7;

    // Mode Device Data must be valid
    int mdd = (int)config.modeDeviceData;
    if (mdd == '0' || mdd == '1')
        mdd -= '0';
    if (mdd != (int)MODE_ACCESS_DOOR && mdd != (int)MODE_ATTENDANCE)
        mdd = (int)MODE_ACCESS_DOOR;
    config.modeDeviceData = (DeviceModeData)mdd;

    // Mode must be valid for selected device type
    int md = (int)config.mode;
    int originalMd = md;

    if (config.modeDeviceData == MODE_ACCESS_DOOR)
    {
        if (md != (int)MODE_NORMAL && md != (int)MODE_OPEN && md != (int)MODE_CLOSE)
            md = (int)MODE_NORMAL;
    }
    else
    {
        if (md != (int)MODE_SCAN && md != (int)MODE_ADD)
            md = (int)MODE_SCAN;
    }

    if (originalMd != md)
    {
        Serial.println("[Config] Mode invalid! Reset from " + String(originalMd) + " to " + String(md));
    }

    config.mode = (DeviceMode)md;
}
