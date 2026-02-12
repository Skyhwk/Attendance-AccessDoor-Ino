#include "storage_manager.h"

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
    // LOG FILE
    // ===============================

    if (!SD.exists(LOG_FILE))
    {
        File f = SD.open(LOG_FILE, FILE_WRITE);
        if (!f)
            return false;
        f.close();
        Serial.println("log.bin created");
    }

    initLog();

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

bool StorageManager::initLog()
{
    if (!SD.exists(LOG_FILE))
    {
        File f = SD.open(LOG_FILE, FILE_WRITE);
        if (!f)
            return false;

        LogHeader header;
        header.writeIndex = 0;
        header.totalWritten = 0;

        f.write((uint8_t *)&header, sizeof(header));
        f.close();
    }
    return true;
}

bool StorageManager::addLog(const LogRecord &input)
{
    File f = SD.open(LOG_FILE, "r+");
    if (!f)
        return false;

    LogHeader header;

    f.read((uint8_t *)&header, sizeof(header));

    uint32_t index = header.writeIndex % MAX_LOG_RECORD;

    LogRecord rec = input;
    rec.sequence = header.totalWritten + 1;

    f.seek(sizeof(LogHeader) + index * sizeof(LogRecord));
    f.write((uint8_t *)&rec, sizeof(rec));

    header.writeIndex++;
    header.totalWritten++;

    f.seek(0);
    f.write((uint8_t *)&header, sizeof(header));

    f.close();
    return true;
}

uint32_t StorageManager::getTotalLogWritten()
{
    File f = SD.open(LOG_FILE, FILE_READ);
    if (!f)
        return 0;

    LogHeader header;
    f.read((uint8_t *)&header, sizeof(header));
    f.close();

    return header.totalWritten;
}
