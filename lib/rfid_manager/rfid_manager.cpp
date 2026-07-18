#include "rfid_manager.h"
#include "config_manager.h"
#include "storage_manager.h"
#include "door_manager.h"
#include "buzzer_manager.h"
#include "lcd_manager.h"
#include "time_global.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "sync_manager.h"
#include <ArduinoJson.h>
#include <rdm6300.h>

RFIDManager RFID;
static Rdm6300 rdm6300;

struct DeferredRfidWork
{
    bool pending = false;
    char rfid[16];
    char nama[32];
    char datetime[20];
    char status[32];
    char iddev[16];
    uint8_t modeDeviceData = 0;
    uint8_t mode = 0;
    bool includeMode = false;
};

static DeferredRfidWork g_deferred;
// Mutex melindungi g_deferred dari race condition antara taskRFID dan taskMQTT
// yang berjalan parallel di ESP32 dual-core tanpa sinkronisasi
static SemaphoreHandle_t g_deferredMutex = nullptr;

static bool isEmptyStr(const char *s);
static void safeCopy(char *dest, const String &src, size_t len);
static void safeCopyCStr(char *dest, const char *src, size_t len);

static const char *modeDeviceDataToString(DeviceModeData modeDeviceData)
{
    return (modeDeviceData == MODE_ATTENDANCE) ? "Mesin Absensi" : "Mesin Akses Pintu";
}

static const char *modeToString(DeviceMode mode)
{
    switch (mode)
    {
    case MODE_NORMAL:
        return "NORMAL";
    case MODE_OPEN:
        return "OPEN";
    case MODE_CLOSE:
        return "CLOSE";
    case MODE_SCAN:
        return "SCAN";
    case MODE_ADD:
        return "ADD";
    default:
        return "UNKNOWN";
    }
}

static void publishRfidLogWithOfflineQueue(const DeviceConfig &cfg,
                                           const char *rfid,
                                           const char *nama,
                                           const char *datetime,
                                           const char *status,
                                           bool includeMode)
{
    bool online = Wifi.isConnected() && MQTT.isConnected();

    if (online)
    {
        StaticJsonDocument<512> docPayload;
        docPayload["topic"] = modeDeviceDataToString(cfg.modeDeviceData);
        docPayload["device"] = String(cfg.iddev);

        JsonObject dataObj = docPayload.createNestedObject("data");
        dataObj["rfid"] = rfid;
        dataObj["nama"] = nama;
        dataObj["datetime"] = datetime;
        dataObj["status"] = status;
        if (includeMode)
            dataObj["mode"] = modeToString(cfg.mode);

        String payload;
        serializeJson(docPayload, payload);

        if (MQTT.publishLog(payload))
        {
            Serial.println("[RFID] Data sent to server: " + payload);
            return;
        }

        Serial.println("[RFID] MQTT publish failed, saving to offline queue");
    }

    bool queued = Sync.addOfflineRecord(
        rfid,
        nama,
        datetime,
        status,
        cfg.iddev,
        (uint8_t)cfg.modeDeviceData,
        (uint8_t)cfg.mode,
        includeMode);

    if (queued)
        Serial.println("[RFID] Data queued for offline sync");
    else
        Serial.println("[RFID] ERROR: Failed to queue offline data!");
}

static void addLocalLog(const DeviceConfig &cfg,
                        const char *rfid,
                        const char *nama,
                        const char *datetime)
{
    LogRecord lr;
    safeCopyCStr(lr.rfid, rfid, sizeof(lr.rfid));
    safeCopyCStr(lr.full_name, nama, sizeof(lr.full_name));
    safeCopyCStr(lr.datetime, datetime, sizeof(lr.datetime));
    safeCopyCStr(lr.iddev, cfg.iddev, sizeof(lr.iddev));
    Storage.addLog(lr);
}

static bool isEmptyStr(const char *s)
{
    return (s == nullptr) || (s[0] == '\0');
}

static void safeCopy(char *dest, const String &src, size_t len)
{
    memset(dest, 0, len);
    strncpy(dest, src.c_str(), len - 1);
}

static void safeCopyCStr(char *dest, const char *src, size_t len)
{
    memset(dest, 0, len);
    if (src != nullptr)
        strncpy(dest, src, len - 1);
}

void RFIDManager::begin(int rxPin)
{
    if (g_deferredMutex == nullptr)
        g_deferredMutex = xSemaphoreCreateMutex();
    rdm6300.begin(rxPin);
    rdm6300.set_tag_timeout(300);
}

String RFIDManager::read()
{
    String tag;
    if (!readTag(tag))
        return "";
    return tag;
}

bool RFIDManager::readTag(String &outTag)
{
    if (!rdm6300.get_new_tag_id())
        return false;

    outTag = String(rdm6300.get_tag_id(), HEX);
    outTag.toLowerCase();
    return outTag.length() > 0;
}

void RFIDManager::loop()
{
    String tag;
    if (!readTag(tag))
        return;
    handleTag(tag);
}

static void deferBackgroundWork(const DeviceConfig &cfg,
                                const String &tag,
                                const String &nama,
                                const String &datetime,
                                const String &status,
                                bool includeMode)
{
    if (g_deferredMutex == nullptr)
        return;

    if (xSemaphoreTake(g_deferredMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        Serial.println("[RFID] deferBackgroundWork: mutex timeout, data discarded");
        return;
    }

    safeCopy(g_deferred.rfid, tag, sizeof(g_deferred.rfid));
    safeCopy(g_deferred.nama, nama, sizeof(g_deferred.nama));
    safeCopy(g_deferred.datetime, datetime, sizeof(g_deferred.datetime));
    safeCopy(g_deferred.status, status, sizeof(g_deferred.status));
    safeCopyCStr(g_deferred.iddev, cfg.iddev, sizeof(g_deferred.iddev));
    g_deferred.modeDeviceData = (uint8_t)cfg.modeDeviceData;
    g_deferred.mode = (uint8_t)cfg.mode;
    g_deferred.includeMode = includeMode;
    g_deferred.pending = true;

    xSemaphoreGive(g_deferredMutex);
}

void RFIDManager::processDeferred()
{
    if (!g_deferred.pending)
        return;

    if (g_deferredMutex == nullptr)
        return;

    if (xSemaphoreTake(g_deferredMutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;

    DeferredRfidWork work = g_deferred;
    g_deferred.pending = false;

    xSemaphoreGive(g_deferredMutex);

    auto &cfg = Config.get();
    publishRfidLogWithOfflineQueue(cfg,
                                   work.rfid,
                                   work.nama,
                                   work.datetime,
                                   work.status,
                                   work.includeMode);
    addLocalLog(cfg, work.rfid, work.nama, work.datetime);
}

void RFIDManager::handleTag(const String &tag)
{
    auto &cfg = Config.get();

    AccessRecord rec;
    bool hasAccess = Storage.findByRFIDFlexible(tag.c_str(), rec);
    const char *nama = hasAccess ? rec.full_name : "";

    if (cfg.modeDeviceData == MODE_ACCESS_DOOR)
    {
        if (cfg.mode != MODE_NORMAL)
        {
            Buzzer.reject();
            LCD.showTemp("Can't", "Scanning..", 2000);
            return;
        }

        if (!hasAccess)
        {
            Buzzer.reject();
            LCD.showTemp("Akses", "Ditolak", 2000);
        }
        else
        {
            Buzzer.granted();
            LCD.showTemp(nama, "Akses diterima", 2000);
            Door.open();
        }

        String datetime = Time.datetime();
        String status = hasAccess ? String("Akses diterima") : String("Akses ditolak");
        deferBackgroundWork(cfg, tag, nama, datetime, status, false);

        Serial.println("[RFID] Tag=" + tag + " access=" + String(hasAccess) + " nama=" + String(nama));
        return;
    }

    if (cfg.modeDeviceData == MODE_ATTENDANCE)
    {
        if (cfg.mode == MODE_SCAN)
        {
            if (!hasAccess)
            {
                Buzzer.reject();
                LCD.showTemp("Absensi", "Ditolak", 2000);
            }
            else
            {
                Buzzer.granted();
                LCD.showTemp(nama, "Terimakasih", 2000);
            }

            String datetime = Time.datetime();
            String status = hasAccess ? String("Accepted") : String("Rejected");
            deferBackgroundWork(cfg, tag, nama, datetime, status, true);

            Serial.println("[RFID] Tag=" + tag + " scan=" + String(hasAccess));
            return;
        }

        if (cfg.mode == MODE_ADD)
        {
            if (isEmptyStr(cfg.topic_publish))
            {
                Buzzer.reject();
                LCD.showTemp("MQTT", "topic empty", 2000);
                return;
            }

            Buzzer.granted();
            LCD.setInfo2(String(tag) + " Sending..");

            StaticJsonDocument<256> doc;
            doc["topic"] = "adduser";
            doc["device"] = String(cfg.iddev);

            StaticJsonDocument<128> data;
            data["rfid"] = tag;
            data["mode"] = "ADD";

            String dataStr;
            serializeJson(data, dataStr);
            doc["data"] = dataStr;

            String payload;
            serializeJson(doc, payload);

            if (!MQTT.publish(cfg.topic_publish, payload.c_str()))
            {
                Buzzer.reject();
                LCD.setInfo2("MQTT Failed");
            }
            return;
        }

        Buzzer.reject();
        LCD.showTemp("Mode", "Invalid", 2000);
        return;
    }

    Buzzer.reject();
    LCD.showTemp("Device", "Invalid", 2000);
}
