# PINTU & ABSEN BIN (ESP32)

Panduan ini ditujukan untuk **development internal** (clone, install, build, upload, konfigurasi, wiring, dan troubleshooting) untuk device ESP32 yang berfungsi sebagai:

- **Access Door** (akses pintu via RFID + relay)
- **Attendance / Absensi** (scan RFID + logging ke SD)

Firmware menggunakan **ESP32 DevKit v1** + Arduino framework (PlatformIO).

---

## 1) Ringkasan arsitektur

- **MCU**: `esp32doit-devkit-v1`
- **Framework**: Arduino (PlatformIO)
- **UI / Portal konfigurasi**: Web portal via `ESP Async WebServer` (port 80)
- **Network**:
  - WiFi STA untuk mode normal
  - WiFi AP + captive DNS untuk mode konfigurasi (tahan tombol)
- **Storage utama**: **microSD (SPI)**
  - `/config.bin` : konfigurasi device
  - `/access.bin` : database akses (binary)
  - `/log.bin` : log ring-buffer (binary)
  - `/assets/*` : file web portal (css/js/html)

---

## 2) Prasyarat

### Software (PC dev)

- **VS Code** + extension **PlatformIO IDE**
- Driver USB-Serial sesuai board (umumnya CH340/CP210x tergantung modul ESP32 DevKit)
- Git

### Hardware minimum untuk uji

- ESP32 DevKit v1
- Modul microSD (SPI) + microSD card
- OLED SSD1306 I2C 128x64 (alamat `0x3C`)
- RFID reader **RDM6300** (125kHz)
- Relay untuk kunci pintu
- Buzzer
- Tombol/touch input untuk:
  - masuk mode portal (hold saat boot)
  - tombol “notouch” untuk buka pintu saat run

---

## 3) Clone project

```bash
git clone https://github.com/Skyhwk/Attendance-AccessDoor-Ino.git
```

Buka folder project ini dari VS Code (PlatformIO).

---

## 4) Build / Upload / Monitor

Konfigurasi env ada di `platformio.ini`:

- `env:esp32doit-devkit-v1`
- `monitor_speed = 115200`

### Build

Di VS Code PlatformIO:

- PlatformIO: **Build**

Atau via terminal:

```bash
pio run
```

### Upload (flash firmware)

Di VS Code PlatformIO:

- PlatformIO: **Upload**

Atau via terminal:

```bash
pio run -t upload
```

### Serial monitor

Di VS Code PlatformIO:

- PlatformIO: **Monitor**

Atau via terminal:

```bash
pio device monitor -b 115200
```

---

## 5) “Flush / Clear” device (opsi pembersihan)

Istilah “flush” di project ini biasanya berarti salah satu dari:

### A) Bersihkan konfigurasi (reset config)

- Dari MQTT command: `{"topic":"reset", ... }` (lihat bagian MQTT)
- Dari Portal: endpoint `/reset` (butuh login portal)

Efek:
- Menghapus file **`/config.bin`** di microSD
- Device reboot → masuk kondisi “NOT CONFIGURED” sampai Anda setup via Portal

### B) Erase flash ESP32 (bersihkan firmware di chip)

Ini menghapus firmware dari flash internal ESP32.

```bash
pio run -t erase
```

Catatan:
- Data konfigurasi project ini **bukan** di flash internal, melainkan di microSD. Jadi erase flash tidak menghapus `/config.bin` (SD).

### C) Bersihkan database akses & log di microSD

File yang dipakai:

- `/access.bin`
- `/log.bin`

Cara cepat (manual):
- Cabut microSD
- Hapus file tersebut dari PC
- Atau format ulang microSD (FAT32)

---

## 6) Persiapan microSD (WAJIB)

### Format

- Format **FAT32**
- Disarankan ukuran cluster default

### Struktur file yang diharapkan di root microSD

- **`/config.bin`** (dibuat saat Anda menyimpan konfigurasi dari Portal)
- **`/access.bin`** (dibuat otomatis saat boot jika belum ada)
- **`/log.bin`** (dibuat otomatis saat boot jika belum ada)
- **`/assets/`** (WAJIB untuk Portal UI)

### Copy asset portal dari folder `data/` ke microSD

Firmware membaca file portal dari path:

- `/assets/portal.css`
- `/assets/portal.js`
- `/assets/portal_login.html`
- `/assets/portal_home.html`
- `/assets/portal_setting.html`

Sumber di repo ada di folder:

- `data/portal.css`
- `data/portal.js`
- `data/portal_login.html`
- `data/portal_home.html`
- `data/portal_setting.html`

Langkah:

1. Buat folder `assets` di root microSD
2. Copy semua file dari `data/` ke `microSD:/assets/`

Contoh hasil akhir (di microSD):

- `X:/assets/portal.css`
- `X:/assets/portal.js`
- `X:/assets/portal_login.html`
- `X:/assets/portal_home.html`
- `X:/assets/portal_setting.html`

Jika folder/file assets tidak ada, Portal akan menampilkan **404 File not found**.

---

## 7) Wiring hardware (PIN mapping)

> Semua pin diambil dari `src/main.cpp` dan driver SD di `lib/storage/sdcard.cpp`.

### A) Relay (kunci pintu)

- **GPIO 25** → `PIN_RELAY`

Perilaku output:
- Boot default **LOW** (safe state)
- Saat `Door.open()` → HIGH selama ~3 detik lalu LOW

### B) Buzzer

- **GPIO 26** → `PIN_BUZZER`

### C) Tombol / Touch untuk masuk Portal + tombol buka pintu

- **GPIO 16** → `PIN_TOUCH_CONFIG`

Fungsi:
- Saat boot: **tahan LOW selama 10 detik** untuk masuk Portal AP mode
- Saat running: jika dibaca LOW → panggil `Door.open()` (fitur “notouch”)

Catatan wiring:
- Pin menggunakan `INPUT_PULLUP` → tombol idealnya **menghubungkan ke GND** saat ditekan.

### D) RFID RDM6300 (UART / 1-wire RX)

- **GPIO 4** → `PIN_RFID_RX`

Saran:
- Pastikan level tegangan kompatibel dengan ESP32 (3.3V logic).

### E) OLED SSD1306 I2C 128x64

- I2C default ESP32 (tanpa override di code):
  - **SDA = GPIO 21**
  - **SCL = GPIO 22**
- Address OLED: **0x3C**

### F) microSD (SPI)

Dipakai oleh driver `SDCard_init()`:

- **CS = GPIO 5**
- **SCK = GPIO 18**
- **MISO = GPIO 19**
- **MOSI = GPIO 23**

Catatan:
- Gunakan modul microSD yang stabil (wiring pendek, supply 3.3V yang kuat).

---

## 8) Mode operasi device

### A) Boot normal (RUN)

Urutan penting di `setup()`:

- Init safe boot relay
- Init LCD, buzzer, door, time
- Init microSD + storage manager
- Cek Portal hold-button (jika hold → masuk CONFIG)
- Load config dari `/config.bin`
- Jika config valid → RUN:
  - WiFi connect
  - MQTT begin
  - Positioning begin
  - RFID begin
  - start tasks (LCD, buzzer, time, rfid, mqtt, notouch)

### B) Mode konfigurasi (CONFIG)

Masuk CONFIG jika:

- Tahan tombol `PIN_TOUCH_CONFIG` (GPIO16) selama **10 detik** saat boot, atau
- Device tidak menemukan `/config.bin` (NOT CONFIGURED)

Saat CONFIG:
- Device akan menjalankan portal (`Portal.loop()`), bukan loop normal.

---

## 9) Portal konfigurasi (Web UI)

Portal berjalan pada port **80**.

### A) AP Mode (tahan tombol saat boot)

- SSID AP: `device <iddev>` atau `device intilab` jika `iddev` kosong
- IP: lihat OLED (ditampilkan sebagai `WiFi.softAPIP()`)
- Captive DNS aktif (permudah akses)

Akses halaman:

- `http://<IP>/login`
- `http://<IP>/home`
- `http://<IP>/setting`

### B) LAN Mode (saat WiFi tersambung)

Jika WiFi sudah connected, portal akan aktif juga di IP lokal device.

- IP: lihat OLED (ditampilkan sebagai `WiFi.localIP()`)

### C) Login portal

Kredensial di firmware saat ini:

- Username: `admin`
- Password: `78baLitni89`

### D) Endpoint penting

- `GET /api/status` → status `iddev`, `ip`, `online`
- `GET /api/config` → baca konfigurasi
- `POST /api/config` → simpan konfigurasi (akan `ESP.restart()`)
- `GET /download/log` → download `log.bin`
- `GET /download/akses` → download `access.bin`
- `GET /reset` → hapus `/config.bin` lalu reboot

---

## 10) Konfigurasi awal (yang WAJIB diisi)

Semua konfigurasi disimpan ke microSD pada file:

- `/config.bin`

Field penting (lihat `DeviceConfig`):

### WiFi

- `ssid`
- `password`
- `dhcp` (`true`/`false`)
- Jika `dhcp=false`, isi juga:
  - `ip`
  - `gateway`
  - `subnet`

### Broker / Server

- `host`
- `port`

Catatan:
- `host` dipakai oleh:
  - MQTT (PubSubClient) sebagai broker address
  - HTTP positioning sync sebagai base URL (otomatis ditambahkan `http://` jika belum ada)

### Identitas device

- `iddev` (dipakai sebagai MQTT clientId dan routing command)

### Mode device data

- `modeDeviceData`:
  - `0` = Access Door
  - `1` = Attendance

### Timezone offset

- `offsetday` = offset jam untuk NTP (contoh WIB = `7`)

### MQTT Topics

- `topic_subscribe` (topic untuk menerima command JSON)
- `topic_publish` (topic untuk publish event seperti add user saat MODE_ADD)

---

## 11) MQTT: format command & daftar command

Device menerima command JSON via topic `topic_subscribe`.

### Format JSON yang wajib

```json
{
  "device": "<iddev>",
  "topic": "<cmd>",
  "data": "<optional>"
}
```

Catatan:
- Command **diabaikan** jika `device` tidak sama dengan `cfg.iddev`.

### Command yang didukung

- `change_moded` dengan `data` salah satu:
  - `normal`
  - `open`
  - `close`
  - `scan` / `scann`
  - `add`

Efek:
- Mengubah `cfg.mode`, menyimpan ke `/config.bin`, dan mengubah perilaku pintu.

- `open`
  - Memanggil `Door.open()`

- `reboot`
  - Buzzer pattern lalu `ESP.restart()`

- `reset`
  - Menghapus `/config.bin` lalu reboot (kembali “NOT CONFIGURED”)

Catatan:
- Command `sync_access` / `add_user` / `delete_user` dll terlihat di code, namun fungsi HTTP sync via MQTT saat ini **di-comment** (tidak aktif).

---

## 12) Positioning / Auto-sync (HTTP)

Saat device RUN dan WiFi sudah tersambung, `Positioning.loop()` akan mencoba **sekali** melakukan sync:

- Request:
  - `GET <host>/v3/public/api/device_intilab?mode=sync&token=intilab_jaya&device=<iddev>`

Respon JSON dibaca:
- `nameDevice` → ditampilkan sebagai nama device di OLED
- `mode` → disimpan menjadi `cfg.mode`
- `path` → URL untuk download `access.bin`

Lalu device download `access.bin` dari `path` dan menulis ke microSD (replace atomik).

---

## 13) Troubleshooting

### A) LCD menampilkan “SD / Storage Error”

Penyebab umum:
- microSD tidak terbaca / wiring SPI salah
- CS pin berbeda dari wiring
- power supply drop

Cek:
- Pastikan wiring SPI sesuai bagian wiring
- Pastikan microSD FAT32
- Coba microSD lain

### B) Portal 404 / halaman kosong

Penyebab:
- Asset tidak ada di microSD

Cek:
- Pastikan file ada di `microSD:/assets/` dan nama persis:
  - `portal.css`, `portal.js`, `portal_login.html`, `portal_home.html`, `portal_setting.html`

### C) Device tidak konek WiFi / selalu reconnect

Cek:
- SSID/password benar
- Jika `dhcp=false`, pastikan IP config valid
- Lihat serial monitor: log "=== WIFI CONFIG ==="

### D) MQTT tidak connect

Cek:
- WiFi harus connected dulu
- `host` dan `port` broker benar
- `topic_subscribe` tidak kosong (kalau ingin menerima command)
- Pastikan broker reachable dari network (firmware melakukan probe connect singkat)

### E) RFID tidak terbaca

Cek:
- Pastikan modul RDM6300 output ke GPIO4 (RX)
- Tegangan logic kompatibel

---

## 14) Checklist sebelum device dianggap “ready”

- microSD sudah FAT32
- Folder `assets/` sudah terisi file portal dari `data/`
- Boot normal berhasil (tidak restart karena SD error)
- Portal bisa dibuka (AP mode atau LAN)
- Konfigurasi sudah diisi dan tersimpan (`/config.bin` terbentuk)
- WiFi connected (OLED menunjukkan Online + IP)
- MQTT connected (lihat log serial)
- Uji RFID:
  - Access Door: kartu valid → relay aktif dan log tersimpan
  - Attendance: scan → log tersimpan

---

## 15) Offline Queue (Auto-Sync Data)

### Overview

Firmware sekarang mendukung **offline queue** untuk memastikan tidak ada data yang hilang saat device offline. Semua RFID scan akan otomatis:

1. **Coba push langsung** ke MQTT jika online
2. **Jika gagal atau offline**: Simpan ke `/offline_data.bin`
3. **Auto-sync saat online**: Device akan otomatis push data ke server saat idle

### File yang digunakan

- `/offline_data.bin` (max 1000 records, ~140 KB)

### Monitoring

Via Portal API:

```bash
GET /api/sync/status          # Check pending count
GET /api/status               # Include offlinePending field
GET /download/offline         # Download offline_data.bin
GET /api/sync/clear           # Clear queue (emergency)
```

Response `/api/status`:

```json
{
  "ok": true,
  "iddev": "device001",
  "ip": "192.168.1.100",
  "online": true,
  "offlinePending": 25
}
```

### Behavior

- **Sync interval**: 5 detik
- **Throttling**: 1 record per cycle (non-blocking)
- **Overflow**: Ring buffer (overwrite oldest jika penuh)
- **Auto-reset**: Counter reset saat semua terkirim

### Serial Monitor

```
[Sync] Pending offline records: 25
[Sync] Record sent successfully. Remaining: 24
[Sync] All offline data synced! Resetting counters.
```

📖 **Dokumentasi lengkap**: Lihat `OFFLINE_QUEUE.md`

---

## 16) Catatan pengembangan

- Konfigurasi disimpan sebagai binary struct ke `/config.bin`. Jika Anda mengubah struct `DeviceConfig`, firmware lama bisa butuh migrasi.
- Log tersimpan ke `log.bin` sebagai ring-buffer header + records. Download via portal untuk diolah di PC.
- **Offline queue**: Data RFID disimpan ke `offline_data.bin` jika offline, auto-sync saat online.
