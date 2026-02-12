#pragma once
#include <Arduino.h>
#include <SD.h>

#define ACCESS_FILE "/access.bin"
#define LOG_FILE "/log.bin"

#define MAX_LOG_RECORD 10000

#pragma pack(push, 1)

struct AccessRecord
{
    char employee_id[16];
    char rfid[16];
    char full_name[32];
};

struct LogHeader
{
    uint32_t writeIndex;
    uint32_t totalWritten;
};

struct LogRecord
{
    uint32_t sequence;
    char rfid[16];
    char full_name[32];
    char datetime[20]; // YYYY-MM-DD HH:MM:SS
    char iddev[16];
};

#pragma pack(pop)

class StorageManager
{
public:
    bool begin();

    // ===== ACCESS =====
    bool clearAccess();
    bool addAccess(const AccessRecord &rec);
    bool replaceAccessFromStream(Stream &stream, size_t contentLength);
    bool findByRFID(const char *rfid, AccessRecord &out);
    uint32_t countAccess();

    // ===== LOG =====
    bool initLog();
    bool addLog(const LogRecord &rec);
    uint32_t getTotalLogWritten();

private:
    void safeCopy(char *dest, const char *src, size_t len);
};

extern StorageManager Storage;
