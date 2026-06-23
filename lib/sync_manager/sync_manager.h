#pragma once
#include <Arduino.h>

#define OFFLINE_DIR "/offline"
#define OFFLINE_QUEUE_FILE "/offline/queue.txt"
#define OFFLINE_STATE_FILE "/offline/state.txt"
#define MAX_OFFLINE_RECORDS 1000

struct OfflineRecord
{
    char rfid[16];
    char full_name[32];
    char datetime[20];
    char status[32];
    char iddev[16];
    uint8_t modeDeviceData;
    uint8_t mode;
    bool includeMode;
    uint32_t sequence;
};

class SyncManager
{
public:
    bool begin();
    void loop();

    bool addOfflineRecord(const char *rfid,
                          const char *nama,
                          const char *datetime,
                          const char *status,
                          const char *iddev,
                          uint8_t modeDeviceData,
                          uint8_t mode,
                          bool includeMode);

    uint32_t getPendingCount();
    bool clearOfflineData();

private:
    bool initFile();
    bool ensureOfflineDir();
    bool loadState();
    bool saveState();
    int countQueueLines();
    bool appendQueueLine(const OfflineRecord &rec);
    bool readQueueLineAt(uint32_t index, OfflineRecord &rec);
    bool dropOldestPendingLine();
    void compactQueueIfNeeded();
    static void sanitizeField(const char *src, char *dest, size_t len);
    static bool parseQueueLine(const char *line, OfflineRecord &rec);
    static bool formatQueueLine(const OfflineRecord &rec, char *out, size_t outLen);

    bool pushNextRecord();
    bool pushRecord(const OfflineRecord &rec);

    uint32_t _readIndex = 0;
    uint32_t _totalLines = 0;
    unsigned long lastSyncAttempt = 0;
    const unsigned long syncInterval = 5000;
    bool _busy = false;
};

extern SyncManager Sync;
