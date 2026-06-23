#include "sync_manager.h"
#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <string.h>
#include "config_manager.h"

class WifiManager
{
public:
    bool isConnected();
};

class MQTTManager
{
public:
    bool isConnected();
    bool publishLog(String payload);
};

extern WifiManager Wifi;
extern MQTTManager MQTT;

SyncManager Sync;

static const char *QUEUE_HEADER = "# rfid|nama|datetime|status|iddev|modeDeviceData|mode|includeMode";

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

void SyncManager::sanitizeField(const char *src, char *dest, size_t len)
{
    if (dest == nullptr || len == 0)
        return;

    dest[0] = '\0';
    if (src == nullptr)
        return;

    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < len; i++)
    {
        char c = src[i];
        if (c == '|' || c == '\n' || c == '\r')
            c = ' ';
        dest[j++] = c;
    }
    dest[j] = '\0';
}

bool SyncManager::formatQueueLine(const OfflineRecord &rec, char *out, size_t outLen)
{
    if (out == nullptr || outLen == 0)
        return false;

    char rfid[16];
    char nama[32];
    char datetime[20];
    char status[32];
    char iddev[16];

    sanitizeField(rec.rfid, rfid, sizeof(rfid));
    sanitizeField(rec.full_name, nama, sizeof(nama));
    sanitizeField(rec.datetime, datetime, sizeof(datetime));
    sanitizeField(rec.status, status, sizeof(status));
    sanitizeField(rec.iddev, iddev, sizeof(iddev));

    int n = snprintf(out, outLen, "%s|%s|%s|%s|%s|%u|%u|%u",
                     rfid, nama, datetime, status, iddev,
                     (unsigned)rec.modeDeviceData,
                     (unsigned)rec.mode,
                     rec.includeMode ? 1u : 0u);
    return n > 0 && (size_t)n < outLen;
}

bool SyncManager::parseQueueLine(const char *line, OfflineRecord &rec)
{
    if (line == nullptr || line[0] == '\0' || line[0] == '#')
        return false;

    char buffer[256];
    strlcpy(buffer, line, sizeof(buffer));

    char *fields[8] = {nullptr};
    int fieldCount = 0;
    char *start = buffer;
    char *p = buffer;

    while (*p && fieldCount < 8)
    {
        if (*p == '|')
        {
            *p = '\0';
            fields[fieldCount++] = start;
            start = p + 1;
        }
        p++;
    }
    if (fieldCount < 8 && start && *start)
        fields[fieldCount++] = start;

    if (fieldCount < 8)
        return false;

    memset(&rec, 0, sizeof(rec));
    strlcpy(rec.rfid, fields[0], sizeof(rec.rfid));
    strlcpy(rec.full_name, fields[1], sizeof(rec.full_name));
    strlcpy(rec.datetime, fields[2], sizeof(rec.datetime));
    strlcpy(rec.status, fields[3], sizeof(rec.status));
    strlcpy(rec.iddev, fields[4], sizeof(rec.iddev));
    rec.modeDeviceData = (uint8_t)atoi(fields[5]);
    rec.mode = (uint8_t)atoi(fields[6]);
    rec.includeMode = (atoi(fields[7]) != 0);
    return true;
}

bool SyncManager::ensureOfflineDir()
{
    if (SD.exists(OFFLINE_DIR))
        return true;
    return SD.mkdir(OFFLINE_DIR);
}

bool SyncManager::saveState()
{
    File f = SD.open(OFFLINE_STATE_FILE, FILE_WRITE);
    if (!f)
        return false;

    f.print("read=");
    f.println(_readIndex);
    f.print("total=");
    f.println(_totalLines);
    f.close();
    return true;
}

bool SyncManager::loadState()
{
    _readIndex = 0;
    _totalLines = countQueueLines();

    if (!SD.exists(OFFLINE_STATE_FILE))
        return saveState();

    File f = SD.open(OFFLINE_STATE_FILE, FILE_READ);
    if (!f)
        return false;

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("read="))
            _readIndex = (uint32_t)line.substring(5).toInt();
        else if (line.startsWith("total="))
            _totalLines = (uint32_t)line.substring(6).toInt();
    }
    f.close();

    uint32_t actual = (uint32_t)countQueueLines();
    if (_totalLines != actual)
        _totalLines = actual;
    if (_readIndex > _totalLines)
        _readIndex = _totalLines;

    return saveState();
}

int SyncManager::countQueueLines()
{
    if (!SD.exists(OFFLINE_QUEUE_FILE))
        return 0;

    File f = SD.open(OFFLINE_QUEUE_FILE, FILE_READ);
    if (!f)
        return 0;

    int count = 0;
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#"))
            continue;
        count++;
    }
    f.close();
    return count;
}

bool SyncManager::initFile()
{
    if (!ensureOfflineDir())
    {
        Serial.println("[Sync] Failed to create offline directory");
        return false;
    }

    if (SD.exists("/offline_data.bin"))
        SD.remove("/offline_data.bin");

    if (!SD.exists(OFFLINE_QUEUE_FILE))
    {
        File f = SD.open(OFFLINE_QUEUE_FILE, FILE_WRITE);
        if (!f)
        {
            Serial.println("[Sync] Failed to create offline queue.txt");
            return false;
        }
        f.println(QUEUE_HEADER);
        f.close();
        Serial.println("[Sync] offline/queue.txt created");
    }

    return loadState();
}

bool SyncManager::begin()
{
    return initFile();
}

void SyncManager::loop()
{
    if (_busy)
        return;

    if (!Wifi.isConnected() || !MQTT.isConnected())
        return;

    unsigned long now = millis();
    if (now - lastSyncAttempt < syncInterval)
        return;

    lastSyncAttempt = now;

    uint32_t pending = getPendingCount();
    if (pending == 0)
        return;

    Serial.println("[Sync] Pending offline records: " + String(pending));

    _busy = true;
    bool success = pushNextRecord();
    _busy = false;

    if (success)
        Serial.println("[Sync] Record sent successfully. Remaining: " + String(getPendingCount()));
    else
        Serial.println("[Sync] Failed to send record. Will retry next cycle.");
}

bool SyncManager::appendQueueLine(const OfflineRecord &rec)
{
    char line[256];
    if (!formatQueueLine(rec, line, sizeof(line)))
        return false;

    File f = SD.open(OFFLINE_QUEUE_FILE, FILE_APPEND);
    if (!f)
        return false;

    f.println(line);
    f.close();
    return true;
}

bool SyncManager::readQueueLineAt(uint32_t index, OfflineRecord &rec)
{
    if (!SD.exists(OFFLINE_QUEUE_FILE))
        return false;

    File f = SD.open(OFFLINE_QUEUE_FILE, FILE_READ);
    if (!f)
        return false;

    uint32_t current = 0;
    bool found = false;

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#"))
            continue;

        if (current == index)
        {
            found = parseQueueLine(line.c_str(), rec);
            break;
        }
        current++;
    }

    f.close();
    return found;
}

bool SyncManager::dropOldestPendingLine()
{
    if (_readIndex >= _totalLines)
        return false;

    File in = SD.open(OFFLINE_QUEUE_FILE, FILE_READ);
    if (!in)
        return false;

    File out = SD.open("/offline/queue.tmp", FILE_WRITE);
    if (!out)
    {
        in.close();
        return false;
    }

    out.println(QUEUE_HEADER);

    uint32_t current = 0;
    bool dropped = false;

    while (in.available())
    {
        String line = in.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#"))
            continue;

        if (current == _readIndex && !dropped)
        {
            dropped = true;
        }
        else
        {
            out.println(line);
        }
        current++;
    }

    in.close();
    out.close();

    SD.remove(OFFLINE_QUEUE_FILE);
    SD.rename("/offline/queue.tmp", OFFLINE_QUEUE_FILE);

    if (_totalLines > 0)
        _totalLines--;

    return dropped;
}

void SyncManager::compactQueueIfNeeded()
{
    if (_readIndex < _totalLines)
        return;

    File f = SD.open(OFFLINE_QUEUE_FILE, FILE_WRITE);
    if (!f)
        return;

    f.println(QUEUE_HEADER);
    f.close();

    _readIndex = 0;
    _totalLines = 0;
    saveState();
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
    while (getPendingCount() >= MAX_OFFLINE_RECORDS)
    {
        Serial.println("[Sync] Offline queue FULL! Dropping oldest record.");
        if (!dropOldestPendingLine())
            break;
    }

    OfflineRecord rec;
    memset(&rec, 0, sizeof(rec));
    sanitizeField(rfid, rec.rfid, sizeof(rec.rfid));
    sanitizeField(nama, rec.full_name, sizeof(rec.full_name));
    sanitizeField(datetime, rec.datetime, sizeof(rec.datetime));
    sanitizeField(status, rec.status, sizeof(rec.status));
    sanitizeField(iddev, rec.iddev, sizeof(rec.iddev));
    rec.modeDeviceData = modeDeviceData;
    rec.mode = mode;
    rec.includeMode = includeMode;
    rec.sequence = _totalLines;

    if (!appendQueueLine(rec))
    {
        Serial.println("[Sync] Failed to append offline record");
        return false;
    }

    _totalLines++;
    if (!saveState())
    {
        Serial.println("[Sync] Failed to save offline state");
        return false;
    }

    Serial.println("[Sync] Offline record added. Total pending: " + String(getPendingCount()));
    return true;
}

uint32_t SyncManager::getPendingCount()
{
    if (_totalLines < _readIndex)
        return 0;
    return _totalLines - _readIndex;
}

bool SyncManager::clearOfflineData()
{
    if (SD.exists(OFFLINE_QUEUE_FILE))
        SD.remove(OFFLINE_QUEUE_FILE);
    if (SD.exists(OFFLINE_STATE_FILE))
        SD.remove(OFFLINE_STATE_FILE);
    if (SD.exists("/offline_data.bin"))
        SD.remove("/offline_data.bin");

    _readIndex = 0;
    _totalLines = 0;
    return initFile();
}

bool SyncManager::pushNextRecord()
{
    if (_readIndex >= _totalLines)
        return false;

    OfflineRecord rec;
    if (!readQueueLineAt(_readIndex, rec))
        return false;

    if (!pushRecord(rec))
        return false;

    _readIndex++;
    saveState();
    compactQueueIfNeeded();

    if (getPendingCount() == 0)
        Serial.println("[Sync] All offline data synced! Queue cleared.");

    return true;
}

bool SyncManager::pushRecord(const OfflineRecord &rec)
{
    auto &cfg = Config.get();
    (void)cfg;

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
        Serial.println("[Sync] Record pushed successfully");
    else
        Serial.println("[Sync] Failed to push record");

    return ok;
}
