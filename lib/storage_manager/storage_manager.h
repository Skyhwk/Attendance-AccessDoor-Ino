#pragma once
#include <Arduino.h>
#include <SD.h>

#define ACCESS_FILE "/access.bin"
#define LOG_DIR "/logs"
#define LOG_RETENTION_DAYS 90

#pragma pack(push, 1)

struct AccessRecord
{
    char employee_id[16];
    char rfid[16];
    char full_name[32];
};

struct LogRecord
{
    uint32_t sequence;
    char rfid[16];
    char full_name[32];
    char datetime[20]; // YYYY-MM-DDTHH:MM:SS
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
    bool applyAccessDownload(Stream &stream, int contentLength);
    bool findByRFID(const char *rfid, AccessRecord &out);
    bool findByRFIDFlexible(const char *tag, AccessRecord &out);
    uint32_t countAccess();

    // ===== LOG =====
    bool initLog();
    bool addLog(const LogRecord &rec);
    void loopLogMaintenance();
    uint32_t getTotalLogWritten();
    String getTodayLogPath() const;

private:
    void safeCopy(char *dest, const char *src, size_t len);
    bool ensureLogDir();
    void pruneOldLogs();
    static void extractDateYmd(const char *datetime, char *out, size_t outLen);
    static bool parseLogFilenameDate(const char *filename, int &y, int &m, int &d);
    static time_t filenameToEpoch(const char *filename);
    int countLogFiles();
    bool findOldestLogFile(char *out, size_t outLen, time_t *outEpoch);
    void deleteLogsOlderThan(time_t cutoff);

    uint32_t _logSequence = 0;
    char _lastPruneDate[11] = "";
    bool _prunePending = false;
};

extern StorageManager Storage;
