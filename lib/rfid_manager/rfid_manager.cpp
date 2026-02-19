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

static bool isEmptyStr(const char *s);
static void safeCopy(char *dest, const String &src, size_t len);

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
                                           const String &rfid,
                                           const String &nama,
                                           const String &datetime,
                                           const String &status,
                                           bool includeMode)
{
    bool online = Wifi.isConnected() && MQTT.isConnected();
    bool pushSuccess = false;

    // Coba push langsung jika online
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

        pushSuccess = MQTT.publishLog(payload);

        if (pushSuccess)
        {
            Serial.println("[RFID] Data sent to server: " + payload);
            return;
        }
        else
        {
            Serial.println("[RFID] MQTT publish failed, saving to offline queue");
        }
    }

    // Jika offline atau push gagal, simpan ke offline queue
    bool queued = Sync.addOfflineRecord(
        rfid.c_str(),
        nama.c_str(),
        datetime.c_str(),
        status.c_str(),
        cfg.iddev,
        (uint8_t)cfg.modeDeviceData,
        (uint8_t)cfg.mode,
        includeMode);

    if (queued)
    {
        Serial.println("[RFID] Data queued for offline sync");
    }
    else
    {
        Serial.println("[RFID] ERROR: Failed to queue offline data!");
    }
}

static void addLocalLog(const DeviceConfig &cfg,
                        const String &rfid,
                        const String &nama,
                        const String &datetime)
{
    LogRecord lr;
    safeCopy(lr.rfid, rfid, sizeof(lr.rfid));
    safeCopy(lr.full_name, nama, sizeof(lr.full_name));
    safeCopy(lr.datetime, datetime, sizeof(lr.datetime));
    safeCopy(lr.iddev, String(cfg.iddev), sizeof(lr.iddev));
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

void RFIDManager::begin(int rxPin)
{
    rdm6300.begin(rxPin);
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

void RFIDManager::handleTag(const String &tag)
{
    auto &cfg = Config.get();

    Serial.println("[RFID] Tag=" + tag);

    AccessRecord rec;
    bool hasAccess = false;
    String tagLower = tag;
    tagLower.toLowerCase();
    String tagUpper = tag;
    tagUpper.toUpperCase();

    if (!hasAccess)
        hasAccess = Storage.findByRFID(tagLower.c_str(), rec);
    if (!hasAccess)
        hasAccess = Storage.findByRFID(tagUpper.c_str(), rec);
    if (!hasAccess && tagLower.length() > 1)
    {
        String trimmed = tagLower;
        while (!hasAccess && trimmed.length() > 1 && trimmed[0] == '0')
        {
            trimmed.remove(0, 1);
            hasAccess = Storage.findByRFID(trimmed.c_str(), rec);
        }
    }
    String nama = hasAccess ? String(rec.full_name) : String("");

    Serial.println("[RFID] tagRfid=" + String(tag));
    Serial.println("[RFID] Nama=" + nama);
    Serial.println("[RFID] Has Access=" + String(hasAccess));
    Serial.println("[RFID] Mode=" + String((int)cfg.mode));
    Serial.println("[RFID] Mode Device Data=" + String((int)cfg.modeDeviceData));
    Serial.println("[RFID] Mode Device Data Tersedia=" + String(MODE_ACCESS_DOOR) + " & " + String(MODE_ATTENDANCE));

    Serial.println(String("[RFID] Kondisi Satu=") + String(cfg.modeDeviceData == MODE_ACCESS_DOOR ? 1 : 0));
    Serial.println(String("[RFID] Kondisi Dua=") + String(cfg.modeDeviceData == MODE_ATTENDANCE ? 1 : 0));

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
            Serial.println("[RFID] Buzzer granted: beeb 2 kali");
            Buzzer.granted();
            Serial.println("[RFID] Akses diterima: " + nama);
            LCD.showTemp(nama, "Akses diterima", 2000);
            Door.open();
        }

        String datetime = Time.datetime();
        String status = hasAccess ? String("Akses diterima") : String("Akses ditolak");
        publishRfidLogWithOfflineQueue(cfg, tag, nama, datetime, status, false);
        addLocalLog(cfg, tag, nama, datetime);
        return;
    }
    else if (cfg.modeDeviceData == MODE_ATTENDANCE)
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
            publishRfidLogWithOfflineQueue(cfg, tag, nama, datetime, status, true);
            addLocalLog(cfg, tag, nama, datetime);
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

            bool ok = MQTT.publish(cfg.topic_publish, payload.c_str());
            if (ok)
            {
                Buzzer.granted();
                LCD.setInfo2(String(tag) + " Sending..");
            }
            else
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
    else
    {
        Buzzer.reject();
        LCD.showTemp("Device", "Invalid", 2000);
    }
}
