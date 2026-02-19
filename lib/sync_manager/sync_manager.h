#pragma once
#include <Arduino.h>

// Forward declarations
class WifiManager;
class MQTTManager;
class ConfigManager;

#define OFFLINE_DATA_FILE "/offline_data.bin"
#define MAX_OFFLINE_RECORDS 1000

#pragma pack(push, 1)

struct OfflineRecord
{
    char rfid[16];
    char full_name[32];
    char datetime[20];
    char status[32];
    char iddev[16];
    uint8_t modeDeviceData; // 0=ACCESS_DOOR, 1=ATTENDANCE
    uint8_t mode;           // MODE_NORMAL, MODE_SCAN, etc
    bool includeMode;       // Apakah mode perlu dikirim
    uint32_t sequence;
};

struct OfflineHeader
{
    uint32_t writeIndex;
    uint32_t readIndex;
    uint32_t totalRecords;
};

#pragma pack(pop)

class SyncManager
{
public:
    bool begin();
    void loop();

    // Tambah data ke offline queue
    bool addOfflineRecord(const char *rfid,
                          const char *nama,
                          const char *datetime,
                          const char *status,
                          const char *iddev,
                          uint8_t modeDeviceData,
                          uint8_t mode,
                          bool includeMode);

    // Get pending count
    uint32_t getPendingCount();

    // Clear all offline data
    bool clearOfflineData();

private:
    bool initFile();
    bool readHeader(OfflineHeader &header);
    bool writeHeader(const OfflineHeader &header);
    bool readRecord(uint32_t index, OfflineRecord &rec);
    bool writeRecord(uint32_t index, const OfflineRecord &rec);

    bool pushNextRecord();
    bool pushRecord(const OfflineRecord &rec);

    unsigned long lastSyncAttempt = 0;
    const unsigned long syncInterval = 5000; // 5 detik check interval
    bool _busy = false;
};

extern SyncManager Sync;
