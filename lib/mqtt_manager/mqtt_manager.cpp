#include "mqtt_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <door_manager.h>
#include "lcd_manager.h"
#include "buzzer_manager.h"

MQTTManager MQTT;

static MQTTManager *g_mqttInstance = nullptr;

static bool isEmptyStr(const char *s)
{
    return (s == nullptr) || (s[0] == '\0');
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

static bool parseUserAccessData(const String &data, String &outRfid, String &outName)
{
    String trimmed = data;
    trimmed.trim();
    if (trimmed.length() == 0)
        return false;

    int sepIdx = trimmed.indexOf('-');
    if (sepIdx <= 0)
        return false;

    outRfid = trimmed.substring(0, sepIdx);
    outName = trimmed.substring(sepIdx + 1);
    outRfid.trim();
    outName.trim();
    outRfid.toLowerCase();

    return outRfid.length() > 0;
}

static bool isAddUserCmd(const String &cmd)
{
    return cmd == "add_user" || cmd == "add_access" || cmd == "adduser";
}

static bool isDeleteUserCmd(const String &cmd)
{
    return cmd == "delete_user" || cmd == "delete_access" || cmd == "deleteuser";
}

static bool isSyncAccessCmd(const String &cmd)
{
    return cmd == "sync_access" || cmd == "sync_user" || cmd == "sync_users" || cmd == "syncuser";
}

void MQTTManager::begin()
{
    auto &cfg = Config.get();
    client.setServer(cfg.host, cfg.port);

    Serial.println("[MQTT] begin() host=" + String(cfg.host) + " port=" + String(cfg.port));

    g_mqttInstance = this;
    if (!callbackInstalled)
    {
        client.setCallback(MQTTManager::callbackThunk);
        callbackInstalled = true;
    }
}

void MQTTManager::loop()
{
    if (Wifi.justConnected())
        onWifiConnected();

    if (!connectIfNeeded())
        return;

    client.loop();
}

bool MQTTManager::publishLog(String payload)
{
    if (!isConnected())
        return false;

    auto &cfg = Config.get();
    String topic = cfg.topic_publish;
    if (topic.isEmpty())
    {
        Serial.println("[MQTT] topic_publish empty");
        return false;
    }

    bool ok = client.publish(topic.c_str(), payload.c_str());
    return ok;
}

bool MQTTManager::publish(const char *topic, const char *payload)
{
    if (!isConnected())
        return false;
    if (topic == nullptr || topic[0] == '\0')
        return false;
    if (payload == nullptr)
        return false;

    Serial.println("[MQTT] publish topic=" + String(topic));
    return client.publish(topic, payload);
}

bool MQTTManager::isConnected()
{
    return client.connected();
}

bool MQTTManager::wifiConfigValid() const
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.ssid))
        return false;
    if (isEmptyStr(cfg.password))
        return false;
    return true;
}

bool MQTTManager::networkReady() const
{
    return Wifi.isConnected();
}


void MQTTManager::onWifiConnected()
{
    lastConnectAttemptMs = 0;
}

bool MQTTManager::connectIfNeeded()
{
    if (!callbackInstalled)
    {
        client.setCallback(MQTTManager::callbackThunk);
        callbackInstalled = true;
        g_mqttInstance = this;
    }

    if (!wifiConfigValid())
    {
        if (client.connected())
            client.disconnect();
        return false;
    }

    if (!networkReady())
    {
        if (client.connected())
            client.disconnect();
        return false;
    }

    if (client.connected())
        return true;

    unsigned long now = millis();
    if (now - lastConnectAttemptMs < connectIntervalMs)
        return false;
    lastConnectAttemptMs = now;

    // brokerReachable() dihapus karena merupakan blocking TCP call yang dapat
    // membekukan taskMQTT selama 5-20 detik saat offline, mencegah Door.update()
    // berjalan dan menyebabkan relay tidak merespon sentuhan RFID / noTouch.
    // PubSubClient.connect() sudah menangani kegagalan connect dengan baik.

    auto &cfg = Config.get();
    String clientId = String(cfg.iddev);

    Serial.println("[MQTT] Connecting as " + clientId);
    bool ok = client.connect(clientId.c_str());
    if (!ok)
    {
        Serial.println("[MQTT] Connect failed, state=" + String(client.state()));
        return false;
    }

    Serial.println("[MQTT] Connected");

    if (!isEmptyStr(cfg.topic_subscribe))
    {
        Serial.println("[MQTT] Subscribing " + String(cfg.topic_subscribe));
        client.subscribe(cfg.topic_subscribe);
    }
    else
    {
        Serial.println("[MQTT] topic_subscribe empty");
    }

    return true;
}

void MQTTManager::callbackThunk(char *topic, byte *payload, unsigned int length)
{
    if (!g_mqttInstance)
        return;
    g_mqttInstance->handleMessage(topic, payload, length);
}

void MQTTManager::handleMessage(const char *topic, const byte *payload, unsigned int length)
{
    Serial.println("[MQTT] Message arrived topic=" + String(topic) + " len=" + String(length));
    String msg;
    msg.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    Serial.println("[MQTT] Payload: " + msg);

    handleCommandJson(String(topic), msg);
}

bool MQTTManager::handleCommandJson(const String &topic, const String &message)
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.iddev))
    {
        Serial.println("[MQTT] Ignore command: iddev empty");
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, message);
    if (err)
    {
        Serial.println("[MQTT] JSON parse failed");
        return false;
    }

    String device = doc["device"] | "";
    if (device != String(cfg.iddev))
    {
        Serial.println("[MQTT] Ignore command for device=" + device);
        return false;
    }

    String cmd = doc["topic"] | "";
    String data = doc["data"] | "";

    Serial.println("[MQTT] Command cmd=" + cmd);

    if (isAddUserCmd(cmd))
        return applyAddUserFromData(data);

    if (isDeleteUserCmd(cmd))
        return applyDeleteUserFromData(data);

    if (isSyncAccessCmd(cmd))
        return syncAccessFromCommand(cmd, data, 15000);

    if (cmd == "change_mode")
    {
        DeviceMode newMode = cfg.mode;

        if (data == "normal")
        {
            if (cfg.modeDeviceData == MODE_ACCESS_DOOR)
            {
                Buzzer.found();
                LCD.setInfo1("");
                newMode = MODE_NORMAL;
            }
        }
        else if (data == "open")
        {
            if (cfg.modeDeviceData == MODE_ACCESS_DOOR)
            {
                Buzzer.granted();
                LCD.setInfo1("Force Open");
                newMode = MODE_OPEN;
            }
        }
        else if (data == "close")
        {
            if (cfg.modeDeviceData == MODE_ACCESS_DOOR)
            {
                Buzzer.reject();
                LCD.setInfo1("Force Close");
                newMode = MODE_CLOSE;
            }
        }
        else if (data == "add")
        {
            if (cfg.modeDeviceData == MODE_ATTENDANCE)
            {
                Buzzer.granted();
                LCD.setInfo1("ADD Card");
                newMode = MODE_ADD;
            }
        }
        else if (data == "scann" || data == "scan")
        {
            if (cfg.modeDeviceData == MODE_ATTENDANCE)
            {
                Buzzer.granted();
                LCD.setInfo1("");
                LCD.showTemp("Tab Card", "", 2000);
                newMode = MODE_SCAN;
            }
        }
        else
        {
            Buzzer.reset();
            Serial.println("[MQTT] change_moded invalid data=" + data);
            return false;
        }

        cfg.mode = newMode;
        bool ok = Config.save();
        Serial.println(String("[MQTT] change_moded saved mode=") + String((int)cfg.mode) + (ok ? " OK" : " FAILED"));

        if (cfg.mode == MODE_OPEN)
            Door.forceOpen();
        else if (cfg.mode == MODE_CLOSE)
            Door.forceClose();
        else
            Door.normal();

        return ok;
    }

    if (cmd == "open")
    {
        Door.noTouchOpen();
    }
    if (cmd == "reboot")
    {
        Buzzer.reboot();
        delay(3000);
        ESP.restart();
    }
    if (cmd == "reset")
    {
        SD.remove("/config.bin");
        Buzzer.reset();
        delay(3000);
        ESP.restart();
    }
    if(cmd == "response")
    {
        int sepIdx = data.indexOf('-');
        if (sepIdx >= 0) {
            String part1 = data.substring(0, sepIdx);
            String part2 = data.substring(sepIdx + 1);
            part1.trim(); // call to trim() acts in-place if using Arduino String
            part2.trim();
            LCD.setInfo1(part1);
            LCD.setInfo2(part2);
        } else {
            String trimmedData = data;
            trimmedData.trim();
            LCD.setInfo1(trimmedData);
            LCD.setInfo2("");
        }
    }

    return false;
}

bool MQTTManager::applyAddUserFromData(const String &data)
{
    String rfid;
    String nama;
    if (!parseUserAccessData(data, rfid, nama))
    {
        Serial.println("[MQTT] add_user invalid data=" + data);
        return false;
    }

    Serial.println("[MQTT] add_user rfid=" + rfid + " nama=" + nama);

    bool ok = Storage.upsertAccess(rfid.c_str(), nama.c_str());
    if (ok)
    {
        Buzzer.granted();
        LCD.showTemp(nama, "User Added", 2000);
    }
    else
    {
        Buzzer.reject();
        Serial.println("[MQTT] add_user FAILED");
    }

    return ok;
}

bool MQTTManager::applyDeleteUserFromData(const String &data)
{
    String rfid;
    String nama;
    if (!parseUserAccessData(data, rfid, nama))
    {
        String trimmed = data;
        trimmed.trim();
        trimmed.toLowerCase();
        int sepIdx = trimmed.indexOf('-');
        rfid = (sepIdx > 0) ? trimmed.substring(0, sepIdx) : trimmed;
        rfid.trim();
    }

    if (rfid.length() == 0)
    {
        Serial.println("[MQTT] delete_user invalid data=" + data);
        return false;
    }

    Serial.println("[MQTT] delete_user rfid=" + rfid);

    bool ok = Storage.deleteAccessByRFID(rfid.c_str());
    if (ok)
    {
        Buzzer.found();
        LCD.showTemp(rfid, "User Deleted", 2000);
    }
    else
    {
        Buzzer.reject();
        Serial.println("[MQTT] delete_user FAILED or not found");
    }

    return ok;
}

String MQTTManager::resolveDownloadUrl(const String &path) const
{
    if (path.length() == 0)
        return path;
    if (path.startsWith("http://") || path.startsWith("https://"))
        return path;

    auto &cfg = Config.get();
    String base = ensureHttpScheme(String(cfg.host));
    if (path.startsWith("/"))
        return base + path;
    return base + "/" + path;
}

bool MQTTManager::fetchAccessPath(const String &cmd, String &outPath)
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.host) || isEmptyStr(cfg.iddev))
        return false;

    String base = ensureHttpScheme(String(cfg.host));
    String url = base + "/v3/public/api/iot-intilab?mode=sync" +
                 "&token=intilab_jaya&device=" + String(cfg.iddev);

    Serial.println("[MQTT] Fetch access path: " + url);

    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(url))
        return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        Serial.println("[MQTT] Fetch path failed code=" + String(code));
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        Serial.println("[MQTT] Fetch path JSON parse failed");
        return false;
    }

    outPath = doc["path"] | "";
    if (outPath.length() == 0)
        outPath = doc["topic"] | "";

    Serial.println("[MQTT] Access path from server: " + outPath);
    return outPath.length() > 0;
}

bool MQTTManager::downloadAccessBin(const String &url, uint32_t timeoutMs)
{
    if (!networkReady())
        return false;

    String downloadUrl = resolveDownloadUrl(url);
    if (downloadUrl.length() == 0)
        return false;

    Serial.println("[MQTT] Download access: " + downloadUrl);

    bool https = isHttpsUrl(downloadUrl);

    HTTPClient http;
    http.setTimeout(timeoutMs);

    if (https)
    {
        WiFiClientSecure secure;
        secure.setInsecure();
        if (!http.begin(secure, downloadUrl))
            return false;
    }
    else
    {
        if (!http.begin(downloadUrl))
            return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        Serial.println("[MQTT] Download failed code=" + String(code));
        http.end();
        return false;
    }

    int len = http.getSize();
    Serial.println("[MQTT] Download size=" + String(len));

    WiFiClient *stream = http.getStreamPtr();
    bool ok = Storage.applyAccessDownload(*stream, len);
    http.end();

    Serial.println(String("[MQTT] Sync access ") + (ok ? "OK" : "FAILED"));
    return ok;
}

bool MQTTManager::syncAccessFromCommand(const String &cmd, const String &data, uint32_t timeoutMs)
{
    if (!networkReady())
        return false;

    String path;

    // Server kirim path/topic relatif lewat MQTT (bukan full URL)
    if (data.length() > 0 && !data.startsWith("http://") && !data.startsWith("https://"))
    {
        path = data;
        Serial.println("[MQTT] Using path from MQTT data: " + path);
    }
    else
    {
        if (!fetchAccessPath(cmd, path))
            return false;
    }

    return downloadAccessBin(path, timeoutMs);
}
