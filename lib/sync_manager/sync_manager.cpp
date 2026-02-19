#include "sync_manager.h"
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "config_manager.h"

// Forward declarations to avoid circular dependencies with WiFi
class WifiManager {
public:
    bool isConnected();
};

class MQTTManager {
public:
    bool isConnected();
    bool publishLog(String payload);
};

// Extern declarations
extern WifiManager Wifi;
extern MQTTManager MQTT;

SyncManager Sync;

static const char *modeDeviceDataToString(uint8_t modeDeviceData)
{
    return (modeDeviceData == 1) ? "Mesin Absensi" : "Mesin Akses Pintu";
}

static const char *modeToString(uint8_t mode)
{
    switch (mode)
    {
    case 0:
        return "NORMAL";
    case 1:
        return "OPEN";
    case 3:
        return "CLOSE";
    case 4:
        return "SCAN";
    case 5:
        return "ADD";
    default:
        return "UNKNOWN";
    }
}

bool SyncManager::begin()
{
    return initFile();
}

void SyncManager::loop()
{
    // Jangan sync jika sedang busy atau WiFi/MQTT tidak ready
    if (_busy)
        return;

    if (!Wifi.isConnected() || !MQTT.isConnected())
        return;

    // Check interval untuk tidak overload
    unsigned long now = millis();
    if (now - lastSyncAttempt < syncInterval)
        return;

    lastSyncAttempt = now;

    // Cek apakah ada pending records
    uint32_t pending = getPendingCount();
    if (pending == 0)
        return;

    Serial.println("[Sync] Pending offline records: " + String(pending));

    // Push satu record per loop cycle untuk tidak block main loop
    _busy = true;
    bool success = pushNextRecord();
    _busy = false;

    if (success)
    {
        Serial.println("[Sync] Record sent successfully. Remaining: " + String(getPendingCount()));
    }
    else
    {
        Serial.println("[Sync] Failed to send record. Will retry next cycle.");
    }
}

bool SyncManager::initFile()
{
    if (!SD.exists(OFFLINE_DATA_FILE))
    {
        File f = SD.open(OFFLINE_DATA_FILE, FILE_WRITE);
        if (!f)
        {
            Serial.println("[Sync] Failed to create offline_data.bin");
            return false;
        }

        OfflineHeader header;
        header.writeIndex = 0;
        header.readIndex = 0;
        header.totalRecords = 0;

        f.write((uint8_t *)&header, sizeof(header));
        f.close();
        Serial.println("[Sync] offline_data.bin created");
    }

    return true;
}

bool SyncManager::readHeader(OfflineHeader &header)
{
    File f = SD.open(OFFLINE_DATA_FILE, FILE_READ);
    if (!f)
        return false;

    size_t read = f.read((uint8_t *)&header, sizeof(header));
    f.close();

    return (read == sizeof(header));
}

bool SyncManager::writeHeader(const OfflineHeader &header)
{
    File f = SD.open(OFFLINE_DATA_FILE, "r+");
    if (!f)
        return false;

    f.seek(0);
    size_t written = f.write((uint8_t *)&header, sizeof(header));
    f.close();

    return (written == sizeof(header));
}

bool SyncManager::readRecord(uint32_t index, OfflineRecord &rec)
{
    File f = SD.open(OFFLINE_DATA_FILE, FILE_READ);
    if (!f)
        return false;

    uint32_t pos = sizeof(OfflineHeader) + (index % MAX_OFFLINE_RECORDS) * sizeof(OfflineRecord);
    f.seek(pos);

    size_t read = f.read((uint8_t *)&rec, sizeof(rec));
    f.close();

    return (read == sizeof(rec));
}

bool SyncManager::writeRecord(uint32_t index, const OfflineRecord &rec)
{
    File f = SD.open(OFFLINE_DATA_FILE, "r+");
    if (!f)
        return false;

    uint32_t pos = sizeof(OfflineHeader) + (index % MAX_OFFLINE_RECORDS) * sizeof(OfflineRecord);
    f.seek(pos);

    size_t written = f.write((uint8_t *)&rec, sizeof(rec));
    f.close();

    return (written == sizeof(rec));
}

bool SyncManager::addOfflineRecord(const char *rfid,
                                   const char *nama,
                                   const char *datetime,
                                   const char *status,
                                   const char *iddev,
                                   uint8_t modeDeviceData,
                                   uint8_t mode,
                                   bool includeMode)
{
    OfflineHeader header;
    if (!readHeader(header))
    {
        Serial.println("[Sync] Failed to read header");
        return false;
    }

    // Check if queue is full
    if (header.totalRecords >= MAX_OFFLINE_RECORDS)
    {
        Serial.println("[Sync] Offline queue FULL! Dropping oldest records.");
        // Masih bisa tulis, akan overwrite record terlama (ring buffer)
    }

    OfflineRecord rec;
    memset(&rec, 0, sizeof(rec));

    strncpy(rec.rfid, rfid, sizeof(rec.rfid) - 1);
    strncpy(rec.full_name, nama, sizeof(rec.full_name) - 1);
    strncpy(rec.datetime, datetime, sizeof(rec.datetime) - 1);
    strncpy(rec.status, status, sizeof(rec.status) - 1);
    strncpy(rec.iddev, iddev, sizeof(rec.iddev) - 1);
    rec.modeDeviceData = modeDeviceData;
    rec.mode = mode;
    rec.includeMode = includeMode;
    rec.sequence = header.writeIndex;

    if (!writeRecord(header.writeIndex, rec))
    {
        Serial.println("[Sync] Failed to write record");
        return false;
    }

    header.writeIndex++;
    if (header.totalRecords < MAX_OFFLINE_RECORDS)
        header.totalRecords++;

    if (!writeHeader(header))
    {
        Serial.println("[Sync] Failed to update header");
        return false;
    }

    Serial.println("[Sync] Offline record added. Total pending: " + String(getPendingCount()));
    return true;
}

uint32_t SyncManager::getPendingCount()
{
    OfflineHeader header;
    if (!readHeader(header))
        return 0;

    if (header.writeIndex >= header.readIndex)
        return header.writeIndex - header.readIndex;
    else
        return 0;
}

bool SyncManager::clearOfflineData()
{
    SD.remove(OFFLINE_DATA_FILE);
    return initFile();
}

bool SyncManager::pushNextRecord()
{
    OfflineHeader header;
    if (!readHeader(header))
        return false;

    // Tidak ada pending records
    if (header.readIndex >= header.writeIndex)
        return false;

    OfflineRecord rec;
    if (!readRecord(header.readIndex, rec))
        return false;

    // Push ke MQTT
    bool success = pushRecord(rec);

    if (success)
    {
        // Update readIndex
        header.readIndex++;
        header.totalRecords--;

        // Jika sudah semua terkirim, reset counter untuk efisiensi
        if (header.readIndex >= header.writeIndex)
        {
            Serial.println("[Sync] All offline data synced! Resetting counters.");
            header.readIndex = 0;
            header.writeIndex = 0;
            header.totalRecords = 0;
        }

        writeHeader(header);
    }

    return success;
}

bool SyncManager::pushRecord(const OfflineRecord &rec)
{
    auto &cfg = Config.get();

    StaticJsonDocument<512> docPayload;
    docPayload["topic"] = modeDeviceDataToString(rec.modeDeviceData);
    docPayload["device"] = String(rec.iddev);

    JsonObject dataObj = docPayload.createNestedObject("data");
    dataObj["rfid"] = String(rec.rfid);
    dataObj["nama"] = String(rec.full_name);
    dataObj["datetime"] = String(rec.datetime);
    dataObj["status"] = String(rec.status);

    if (rec.includeMode)
        dataObj["mode"] = modeToString(rec.mode);

    String payload;
    serializeJson(docPayload, payload);

    Serial.println("[Sync] Pushing offline record: " + payload);

    bool ok = MQTT.publishLog(payload);

    if (ok)
    {
        Serial.println("[Sync] Record pushed successfully");
    }
    else
    {
        Serial.println("[Sync] Failed to push record");
    }

    return ok;
}
