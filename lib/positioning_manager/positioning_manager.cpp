#include "positioning_manager.h"

#include "config_manager.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "door_manager.h"
#include "lcd_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

PositioningManager Positioning;

static bool isEmptyStr(const char *s)
{
    return (s == nullptr) || (s[0] == '\0');
}

static DeviceMode parseMode(const String &mode)
{
    if (mode == "normal")
        return MODE_NORMAL;
    if (mode == "open")
        return MODE_OPEN;
    if (mode == "close")
        return MODE_CLOSE;
    if (mode == "scann" || mode == "scan")
        return MODE_SCAN;
    if (mode == "add")
        return MODE_ADD;
    return MODE_NORMAL;
}

static bool isHttpsUrl(const String &url)
{
    return url.startsWith("https://");
}

static String ensureHttpScheme(const String &host)
{
    if (host.startsWith("http://") || host.startsWith("https://"))
        return host;
    return String("http://") + host;
}

void PositioningManager::begin()
{
    _pending = true;
    _synced = false;
    _attempted = false;
}

bool PositioningManager::isSynced() const
{
    return _synced;
}

void PositioningManager::loop()
{
    if (!_pending)
        return;

    if (!Wifi.isConnected())
        return;

    if (_attempted)
        return;

    _attempted = true;
    _synced = doSync();
    _pending = false;
}

bool PositioningManager::doSync()
{
    String name;
    String modeStr;
    String path;

    if (!fetchInfo(name, modeStr, path))
        return false;

    if (name.length() > 0)
        LCD.setDeviceName(name);

    auto &cfg = Config.get();

    // Hanya update mode jika server mengirim nilai yang valid (tidak kosong)
    if (modeStr.length() > 0)
    {
        DeviceMode newMode = parseMode(modeStr);
        Serial.println("[Positioning] Update mode from server: " + modeStr + " -> " + String((int)newMode));
        cfg.mode = newMode;
    }
    else
    {
        Serial.println("[Positioning] Server tidak mengirim mode, tetap gunakan mode saat ini: " + String((int)cfg.mode));
    }

    bool saved = Config.save();

    if (!saved)
        return false;

    if (path.length() == 0)
        return false;

    return downloadAccessBin(path, 20000);
}

bool PositioningManager::fetchInfo(String &outName, String &outMode, String &outPath)
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.host) || isEmptyStr(cfg.iddev))
        return false;

    String base = ensureHttpScheme(String(cfg.host));
    String url = base + "/v3/public/api/iot-intilab?mode=sync&token=intilab_jaya&device=" + String(cfg.iddev);

    Serial.println("[Positioning] Syncing: " + url);

    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(url))
        return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, body);
    if (err)
        return false;

    outName = doc["nameDevice"] | "";
    outMode = doc["mode"] | "";
    outPath = doc["path"] | "";

    Serial.println("[Positioning] Synced: " + outName + " " + outMode + " " + outPath);

    return true;
}

bool PositioningManager::downloadAccessBin(const String &url, uint32_t timeoutMs)
{
    if (!Wifi.isConnected())
        return false;

    bool https = isHttpsUrl(url);

    HTTPClient http;
    http.setTimeout(timeoutMs);

    if (https)
    {
        WiFiClientSecure secure;
        secure.setInsecure();
        if (!http.begin(secure, url))
            return false;
    }
    else
    {
        if (!http.begin(url))
            return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0)
    {
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    bool ok = Storage.replaceAccessFromStream(*stream, (size_t)len);
    http.end();
    return ok;
}
