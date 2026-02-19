# Offline Queue Feature

## 📋 Overview

Fitur **Offline Queue** memastikan tidak ada data yang hilang saat device offline. Semua RFID scan (baik Access Door maupun Attendance) akan otomatis disimpan ke antrian offline dan di-push ke server secara otomatis saat device kembali online.

---

## 🎯 Flow Diagram

```
RFID Scan
    ↓
[Coba push ke MQTT]
    ↓
Online? ──YES──> Push Success? ──YES──> [DONE]
    ↓                   ↓
    NO                 NO
    ↓                   ↓
[Simpan ke offline_data.bin]
    ↓
[Queue untuk sync nanti]
    ↓
[Saat online & idle]
    ↓
[Auto-push satu per satu]
    ↓
[Hapus dari queue]
```

---

## 📁 File Storage

### `/offline_data.bin`

Struktur file:

```cpp
[OfflineHeader]
├── writeIndex (uint32_t)
├── readIndex (uint32_t)
└── totalRecords (uint32_t)

[OfflineRecord 0]
[OfflineRecord 1]
...
[OfflineRecord MAX_OFFLINE_RECORDS-1]
```

### OfflineRecord Structure

```cpp
struct OfflineRecord
{
    char rfid[16];
    char full_name[32];
    char datetime[20];        // YYYY-MM-DD HH:MM:SS
    char status[32];          // "Akses diterima", "Accepted", dll
    char iddev[16];
    uint8_t modeDeviceData;   // 0=ACCESS_DOOR, 1=ATTENDANCE
    uint8_t mode;             // MODE_NORMAL, MODE_SCAN, dll
    bool includeMode;         // Apakah mode perlu dikirim ke server
    uint32_t sequence;
};
```

---

## 🔧 Konfigurasi

### Kapasitas Queue

Defined di `lib/sync_manager/sync_manager.h`:

```cpp
#define MAX_OFFLINE_RECORDS 1000
```

**Default: 1000 records**

### Sync Interval

Defined di `lib/sync_manager/sync_manager.cpp`:

```cpp
const unsigned long syncInterval = 5000; // 5 detik
```

**Default: 5 detik** (check interval untuk auto-sync)

---

## 📊 Monitoring

### Via Serial Monitor

```
[Sync] Pending offline records: 25
[Sync] Record sent successfully. Remaining: 24
[Sync] Record sent successfully. Remaining: 23
...
[Sync] All offline data synced! Resetting counters.
```

### Via Portal API

#### Get Sync Status

```
GET /api/sync/status
```

Response:
```json
{
  "ok": true,
  "pending": 25
}
```

#### Get Device Status (includes pending count)

```
GET /api/status
```

Response:
```json
{
  "ok": true,
  "iddev": "device001",
  "ip": "192.168.1.100",
  "online": true,
  "offlinePending": 25
}
```

#### Clear Offline Queue (Manual)

```
GET /api/sync/clear
```

Response:
```json
{
  "ok": true,
  "message": "Offline data cleared"
}
```

#### Download Offline Data

```
GET /download/offline
```

Download file `offline_data.bin` untuk analisis.

---

## 🚀 Cara Kerja

### 1. Saat RFID di-scan

Dalam `rfid_manager.cpp`, fungsi `publishRfidLogWithOfflineQueue()`:

1. **Cek koneksi**: WiFi connected AND MQTT connected?
2. **Jika Online**: Coba push langsung ke MQTT
   - **Success**: Selesai ✅
   - **Failed**: Lanjut ke step 3
3. **Jika Offline atau Failed**: Simpan ke `offline_data.bin`

### 2. Auto-Sync di Background

Task MQTT (`taskMQTT` di `main.cpp`) memanggil `Sync.loop()` setiap 10ms.

`Sync.loop()` behavior:
- **Check interval**: 5 detik sekali
- **Check koneksi**: WiFi & MQTT harus connected
- **Check pending**: Ada data dalam queue?
- **Push**: Kirim **satu record per cycle**
  - Menggunakan throttling agar tidak overload
  - Non-blocking (tidak mengganggu RFID scan)

### 3. Queue Management

- **FIFO** (First In First Out)
- **Ring Buffer**: Jika penuh (1000 records), overwrite record terlama
- **Auto-reset**: Saat semua terkirim, reset counter untuk efisiensi

---

## 🎛️ Integration Points

### main.cpp

```cpp
#include "sync_manager.h"

void setup()
{
    // ...
    Sync.begin(); // Init offline data queue
    // ...
}

void taskMQTT(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            MQTT.loop();
            Positioning.loop();
            Sync.loop(); // Auto-sync offline data
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### rfid_manager.cpp

```cpp
#include "sync_manager.h"

// Replace publishRfidLogIfOnline() dengan publishRfidLogWithOfflineQueue()
publishRfidLogWithOfflineQueue(cfg, tag, nama, datetime, status, includeMode);
```

---

## 🔍 Troubleshooting

### Queue penuh terus-menerus

**Penyebab:**
- Server down berkepanjangan
- MQTT broker tidak reachable
- Network issue

**Solusi:**
1. Check koneksi WiFi dan MQTT
2. Verifikasi broker address dan port
3. Jika emergency, clear queue via portal: `GET /api/sync/clear`

### Data tidak ter-sync otomatis

**Check:**
1. WiFi connected? Check `WiFi.isConnected()`
2. MQTT connected? Check `MQTT.isConnected()`
3. Topic publish configured? Check `cfg.topic_publish`
4. Check serial monitor untuk error messages

### Performance issue

**Gejala:** Device jadi lambat saat sync

**Root cause:** Tidak seharusnya terjadi karena:
- Throttling: 5 detik interval
- Non-blocking: 1 record per cycle
- Task priority: Low priority (1)

**Jika tetap terjadi:**
- Increase `syncInterval` dari 5000 ke 10000ms
- Check SD card performance (gunakan SD card yang lebih cepat)

---

## 📈 Performance Metrics

### Memory Usage

- **OfflineHeader**: 12 bytes
- **OfflineRecord**: 144 bytes per record
- **Max file size**: ~140 KB (1000 records)

### Sync Speed

- **Throttle**: 1 record per 5 seconds
- **1000 records**: ~83 minutes (worst case)
- **Typical**: Jauh lebih cepat karena device biasanya tidak penuh offline

### Reliability

- **Data integrity**: Binary format dengan atomic operations
- **Overflow handling**: Ring buffer (overwrite oldest)
- **Error recovery**: Retry otomatis setiap 5 detik

---

## 🛡️ Best Practices

1. **Monitor pending count** secara berkala via portal
2. **Backup offline_data.bin** berkala untuk audit
3. **Set alert** jika pending > threshold (e.g., 500)
4. **Test failover** secara berkala (simulasi offline)
5. **Verify sync** dengan membandingkan log.bin vs server database

---

## 🔄 Migration dari Versi Lama

Jika Anda upgrade dari firmware lama tanpa offline queue:

1. Flash firmware baru
2. Device akan auto-create `/offline_data.bin` saat boot
3. Data lama di `/log.bin` tetap ada (tidak terpengaruh)
4. Tidak perlu action manual

---

## 📞 API Reference

### SyncManager Class

```cpp
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
};

extern SyncManager Sync;
```

### Usage Example

```cpp
// Add record manually
Sync.addOfflineRecord(
    "abcd1234",              // rfid
    "John Doe",              // nama
    "2026-02-18 10:30:00",  // datetime
    "Accepted",              // status
    "device001",             // iddev
    MODE_ATTENDANCE,         // modeDeviceData
    MODE_SCAN,               // mode
    true                     // includeMode
);

// Get pending count
uint32_t pending = Sync.getPendingCount();
Serial.println("Pending: " + String(pending));

// Clear all offline data (emergency)
Sync.clearOfflineData();
```

---

## ✅ Testing Checklist

- [ ] Device boot with empty `/offline_data.bin`
- [ ] RFID scan saat online → langsung terkirim
- [ ] RFID scan saat offline → masuk queue
- [ ] Disconnect WiFi → scan beberapa kali → check pending count
- [ ] Reconnect WiFi → verify auto-sync
- [ ] Fill queue hingga penuh (1000) → verify overflow handling
- [ ] Check portal API `/api/sync/status`
- [ ] Download `/download/offline` via portal
- [ ] Clear queue via `/api/sync/clear`
- [ ] Monitor serial output untuk sync messages

---

**Last Updated**: 2026-02-18  
**Version**: 1.0  
**Author**: ESP32 IoT Team
