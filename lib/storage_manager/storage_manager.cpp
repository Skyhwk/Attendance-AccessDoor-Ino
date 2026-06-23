#include "storage_manager.h"
#include "time_global.h"
#include <time.h>
#include <string.h>

StorageManager Storage;

bool StorageManager::begin()
{
    if (!SD.begin())
        return false;

    // ===============================
    // ACCESS FILE
    // ===============================

    if (!SD.exists(ACCESS_FILE))
    {
        // File belum ada → buat kosong
        File f = SD.open(ACCESS_FILE, FILE_WRITE);
        if (!f)
            return false;
        f.close();
        Serial.println("access.bin created");
    }
    else
    {
        // File ada → cek validitas
        File f = SD.open(ACCESS_FILE, FILE_READ);
        if (!f)
            return false;

        if (f.size() % sizeof(AccessRecord) != 0)
        {
            Serial.println("Access file corrupted. Resetting...");
            f.close();
            SD.remove(ACCESS_FILE);

            File nf = SD.open(ACCESS_FILE, FILE_WRITE);
            if (!nf)
                return false;
            nf.close();
            Serial.println("access.bin recreated");
        }
        else
        {
            f.close();
        }
    }

    // ===============================
    // LOG DIRECTORY
    // ===============================

    if (!ensureLogDir())
        return false;

    initLog();
    pruneOldLogs();

    return true;
}

void StorageManager::safeCopy(char *dest, const char *src, size_t len)
{
    memset(dest, 0, len);
    strncpy(dest, src, len - 1);
}

//
// ====================== ACCESS ======================
//

bool StorageManager::clearAccess()
{
    SD.remove(ACCESS_FILE);
    File f = SD.open(ACCESS_FILE, FILE_WRITE);
    if (!f)
        return false;
    f.close();
    return true;
}

bool StorageManager::addAccess(const AccessRecord &rec)
{
    File f = SD.open(ACCESS_FILE, FILE_APPEND);
    if (!f)
        return false;

    f.write((uint8_t *)&rec, sizeof(rec));
    f.close();
    return true;
}

bool StorageManager::replaceAccessFromStream(Stream &stream, size_t contentLength)
{
    File f = SD.open("/access.tmp", FILE_WRITE);
    if (!f)
        return false;

    size_t total = 0;
    uint8_t buffer[512];

    while (total < contentLength)
    {
        int available = stream.available();
        if (available)
        {
            int readLen = stream.readBytes((char *)buffer, min((int)sizeof(buffer), available));
            f.write(buffer, readLen);
            total += readLen;
        }
    }

    f.close();

    if (total > 0 && total % sizeof(AccessRecord) != 0)
    {
        SD.remove("/access.tmp");
        Serial.println("[Storage] Invalid access.bin size");
        return false;
    }

    SD.remove(ACCESS_FILE);
    SD.rename("/access.tmp", ACCESS_FILE);

    return true;
}

bool StorageManager::applyAccessDownload(Stream &stream, int contentLength)
{
    if (contentLength == 0)
    {
        Serial.println("[Storage] Empty access.bin from server, clearing local access");
        return clearAccess();
    }

    if (contentLength > 0)
        return replaceAccessFromStream(stream, (size_t)contentLength);

    File f = SD.open("/access.tmp", FILE_WRITE);
    if (!f)
        return false;

    uint8_t buffer[512];
    size_t total = 0;
    unsigned long lastDataMs = millis();
    const unsigned long idleTimeoutMs = 3000;

    while (millis() - lastDataMs < idleTimeoutMs)
    {
        int available = stream.available();
        if (available > 0)
        {
            int readLen = stream.readBytes((char *)buffer, min((int)sizeof(buffer), available));
            if (readLen > 0)
            {
                f.write(buffer, readLen);
                total += readLen;
                lastDataMs = millis();
            }
        }
        else
        {
            delay(10);
        }
    }

    f.close();

    if (total == 0)
    {
        SD.remove("/access.tmp");
        Serial.println("[Storage] Empty access.bin from server (chunked), clearing local access");
        return clearAccess();
    }

    if (total % sizeof(AccessRecord) != 0)
    {
        SD.remove("/access.tmp");
        Serial.println("[Storage] Invalid access.bin size");
        return false;
    }

    SD.remove(ACCESS_FILE);
    SD.rename("/access.tmp", ACCESS_FILE);
    return true;
}

bool StorageManager::findByRFID(const char *rfid, AccessRecord &out)
{
    File f = SD.open(ACCESS_FILE, FILE_READ);
    if (!f)
        return false;

    AccessRecord rec;

    while (f.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec))
    {
        if (strcmp(rec.rfid, rfid) == 0)
        {
            out = rec;
            f.close();
            return true;
        }
    }

    f.close();
    return false;
}

bool StorageManager::findByRFIDFlexible(const char *tag, AccessRecord &out)
{
    if (tag == nullptr || tag[0] == '\0')
        return false;

    char lower[16];
    char upper[16];
    char trimmed[16];
    const char *candidates[3];
    int candidateCount = 0;

    strlcpy(lower, tag, sizeof(lower));
    for (char *p = lower; *p; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p - 'A' + 'a');
    }

    strlcpy(upper, lower, sizeof(upper));
    for (char *p = upper; *p; p++)
    {
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    }

    strlcpy(trimmed, lower, sizeof(trimmed));
    while (trimmed[0] == '0' && trimmed[1] != '\0')
        memmove(trimmed, trimmed + 1, strlen(trimmed));

    candidates[candidateCount++] = lower;
    if (strcmp(upper, lower) != 0)
        candidates[candidateCount++] = upper;
    if (strcmp(trimmed, lower) != 0 && strcmp(trimmed, upper) != 0)
        candidates[candidateCount++] = trimmed;

    File f = SD.open(ACCESS_FILE, FILE_READ);
    if (!f)
        return false;

    AccessRecord rec;
    while (f.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec))
    {
        for (int i = 0; i < candidateCount; i++)
        {
            if (strcmp(rec.rfid, candidates[i]) == 0)
            {
                out = rec;
                f.close();
                return true;
            }
        }
    }

    f.close();
    return false;
}

uint32_t StorageManager::countAccess()
{
    File f = SD.open(ACCESS_FILE, FILE_READ);
    if (!f)
        return 0;

    uint32_t size = f.size();
    f.close();

    return size / sizeof(AccessRecord);
}

//
// ====================== LOG ======================
//

bool StorageManager::ensureLogDir()
{
    if (SD.exists(LOG_DIR))
        return true;

    if (SD.mkdir(LOG_DIR))
    {
        Serial.println("[Storage] logs directory created");
        return true;
    }

    Serial.println("[Storage] Failed to create logs directory");
    return false;
}

void StorageManager::extractDateYmd(const char *datetime, char *out, size_t outLen)
{
    if (outLen < 11)
        return;

    if (datetime != nullptr && datetime[0] != '\0' && strlen(datetime) >= 10)
    {
        memcpy(out, datetime, 10);
        out[10] = '\0';
        return;
    }

    String today = Time.date();
    strlcpy(out, today.c_str(), outLen);
}

bool StorageManager::parseLogFilenameDate(const char *filename, int &y, int &m, int &d)
{
    if (filename == nullptr)
        return false;

    size_t len = strlen(filename);
    if (len != 14 || strcmp(filename + 10, ".txt") != 0)
        return false;

    return sscanf(filename, "%d-%d-%d.txt", &y, &m, &d) == 3;
}

time_t StorageManager::filenameToEpoch(const char *filename)
{
    int y = 0, m = 0, d = 0;
    if (!parseLogFilenameDate(filename, y, m, d))
        return 0;

    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    return mktime(&t);
}

int StorageManager::countLogFiles()
{
    if (!SD.exists(LOG_DIR))
        return 0;

    File root = SD.open(LOG_DIR);
    if (!root || !root.isDirectory())
    {
        if (root)
            root.close();
        return 0;
    }

    int count = 0;
    File entry = root.openNextFile();
    while (entry)
    {
        const char *rawName = entry.name();
        const char *name = strrchr(rawName, '/');
        name = (name != nullptr) ? name + 1 : rawName;

        int y = 0, m = 0, d = 0;
        if (parseLogFilenameDate(name, y, m, d))
            count++;

        entry.close();
        entry = root.openNextFile();
    }

    root.close();
    return count;
}

bool StorageManager::findOldestLogFile(char *out, size_t outLen, time_t *outEpoch)
{
    if (out == nullptr || outLen == 0)
        return false;

    out[0] = '\0';
    if (outEpoch)
        *outEpoch = 0;

    if (!SD.exists(LOG_DIR))
        return false;

    File root = SD.open(LOG_DIR);
    if (!root || !root.isDirectory())
    {
        if (root)
            root.close();
        return false;
    }

    bool found = false;
    time_t oldestEpoch = 0;
    char oldestName[16] = "";

    File entry = root.openNextFile();
    while (entry)
    {
        const char *rawName = entry.name();
        const char *name = strrchr(rawName, '/');
        name = (name != nullptr) ? name + 1 : rawName;

        time_t epoch = filenameToEpoch(name);
        if (epoch > 0 && (!found || epoch < oldestEpoch))
        {
            found = true;
            oldestEpoch = epoch;
            strlcpy(oldestName, name, sizeof(oldestName));
        }

        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    if (!found)
        return false;

    strlcpy(out, oldestName, outLen);
    if (outEpoch)
        *outEpoch = oldestEpoch;
    return true;
}

void StorageManager::deleteLogsOlderThan(time_t cutoff)
{
    if (!SD.exists(LOG_DIR))
        return;

    File root = SD.open(LOG_DIR);
    if (!root || !root.isDirectory())
    {
        if (root)
            root.close();
        return;
    }

    File entry = root.openNextFile();
    while (entry)
    {
        const char *rawName = entry.name();
        const char *name = strrchr(rawName, '/');
        name = (name != nullptr) ? name + 1 : rawName;

        time_t epoch = filenameToEpoch(name);
        if (epoch > 0 && epoch < cutoff)
        {
            char path[32];
            snprintf(path, sizeof(path), "%s/%s", LOG_DIR, name);
            if (SD.remove(path))
                Serial.println(String("[Storage] Deleted old log: ") + path);
        }

        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void StorageManager::pruneOldLogs()
{
    time_t now = time(nullptr);
    bool timeValid = now > (86400LL * 365);
    if (timeValid)
    {
        time_t cutoff = now - ((time_t)LOG_RETENTION_DAYS * 86400LL);
        deleteLogsOlderThan(cutoff);
    }

    char oldestName[16];
    while (countLogFiles() > LOG_RETENTION_DAYS)
    {
        if (!findOldestLogFile(oldestName, sizeof(oldestName), nullptr))
            break;

        char path[32];
        snprintf(path, sizeof(path), "%s/%s", LOG_DIR, oldestName);
        if (!SD.remove(path))
            break;

        Serial.println(String("[Storage] Deleted oldest log: ") + path);
    }
}

void StorageManager::loopLogMaintenance()
{
    if (!_prunePending)
        return;

    _prunePending = false;
    pruneOldLogs();
}

bool StorageManager::initLog()
{
    if (!ensureLogDir())
        return false;

    extractDateYmd(nullptr, _lastPruneDate, sizeof(_lastPruneDate));
    return true;
}

bool StorageManager::addLog(const LogRecord &input)
{
    if (!ensureLogDir())
        return false;

    LogRecord rec = input;
    rec.sequence = ++_logSequence;

    char dateYmd[11];
    extractDateYmd(rec.datetime, dateYmd, sizeof(dateYmd));
    if (strcmp(_lastPruneDate, dateYmd) != 0)
    {
        strlcpy(_lastPruneDate, dateYmd, sizeof(_lastPruneDate));
        _prunePending = true;
    }

    char path[32];
    snprintf(path, sizeof(path), "%s/%s.txt", LOG_DIR, dateYmd);
    bool isNew = !SD.exists(path);

    File f = SD.open(path, isNew ? FILE_WRITE : FILE_APPEND);
    if (!f)
        return false;

    if (isNew)
        f.println("datetime,rfid,nama,iddev,sequence");

    f.print(rec.datetime);
    f.print(',');
    f.print(rec.rfid);
    f.print(',');
    f.print(rec.full_name);
    f.print(',');
    f.print(rec.iddev);
    f.print(',');
    f.println(rec.sequence);

    f.close();
    return true;
}

uint32_t StorageManager::getTotalLogWritten()
{
    return _logSequence;
}

String StorageManager::getTodayLogPath() const
{
    return String(LOG_DIR) + "/" + Time.date() + ".txt";
}

