#pragma once
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>

class MQTTManager
{
public:
    void begin();
    void loop();
    bool publishLog(String payload);

    bool publish(const char *topic, const char *payload);

    bool isConnected();

private:
    bool wifiConfigValid() const;
    bool networkReady() const;
    bool brokerReachable(uint32_t timeoutMs);

    bool connectIfNeeded();
    void onWifiConnected();

    static void callbackThunk(char *topic, byte *payload, unsigned int length);
    void handleMessage(const char *topic, const byte *payload, unsigned int length);

    bool handleCommandJson(const String &topic, const String &message);
    bool applyAddUserFromData(const String &data);
    bool applyDeleteUserFromData(const String &data);
    bool syncAccessFromCommand(const String &cmd, const String &data, uint32_t timeoutMs);
    bool fetchAccessPath(const String &cmd, String &outPath);
    bool downloadAccessBin(const String &url, uint32_t timeoutMs);
    String resolveDownloadUrl(const String &path) const;

    WiFiClient espClient;
    PubSubClient client{espClient};

    unsigned long lastConnectAttemptMs = 0;
    const unsigned long connectIntervalMs = 5000;
    bool callbackInstalled = false;
};

extern MQTTManager MQTT;
