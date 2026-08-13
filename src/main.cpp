#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include "buzzer_manager.h"
#include "config_manager.h"
#include "door_manager.h"
#include "lcd_manager.h"
#include "mqtt_manager.h"
#include "portal_config.h"
#include "positioning_manager.h"
#include "rfid_manager.h"
#include "sdcard.h"
#include "storage_manager.h"
#include "sync_manager.h"
#include "time_global.h"
#include "wifi_manager.h"

#define PIN_RELAY 25
#define PIN_BUZZER 26
#define PIN_TOUCH_CONFIG 17
#define PIN_CONFIG 0
#define PIN_RFID_RX 4

// ================= DEVICE STATE =================
enum DeviceState { STATE_BOOT, STATE_CONFIG, STATE_RUN };

DeviceState deviceState = STATE_BOOT;

// ================= TIMING =================
unsigned long bootTimeMs = 0;
unsigned long lastApplyModeMs = 0;

int state_notouch = 0;
// ================= TASK HANDLES =================
TaskHandle_t taskHandleRFID;
TaskHandle_t taskHandleMQTT;
TaskHandle_t taskHandleTime;
TaskHandle_t taskHandleLCD;
TaskHandle_t taskHandleBuzzer;
// TaskHandle_t taskHandleNotouch;

static void applyModeFromConfigToSystem() {
  auto &cfg = Config.get();

  if (cfg.modeDeviceData == MODE_ACCESS_DOOR) {
    Serial.println("[Main] Mode Access Door = " + String((int)cfg.mode));
    if (cfg.mode == MODE_OPEN) {
      Serial.println("Force Open");
      Door.forceOpen();
      LCD.setInfo1("Force Open");
    } else if (cfg.mode == MODE_CLOSE) {
      Serial.println("Force Close");
      Door.forceClose();
      LCD.setInfo1("Force Close");
    } else {
      Serial.println("Normal");
      Door.normal();
      LCD.setInfo1("");
    }

    // return;
  } else if (cfg.modeDeviceData == MODE_ATTENDANCE) {
    if (cfg.mode == MODE_ADD) {
      Serial.println("Mode ADD");
      LCD.setInfo1("ADD Card");
    } else {
      Serial.println("Mode Scan");
      LCD.setInfo1("");
    }
    // return;
  }
}

// ================= SAFE BOOT =================
void safeBoot() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW); // RELAY SAFE STATE
  pinMode(PIN_TOUCH_CONFIG, INPUT_PULLUP);
}

// ================= TASKS =================

void taskRFID(void *pv) {
  for (;;) {
    if (deviceState == STATE_RUN) {
      RFID.loop();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskMQTT(void *pv) {
  for (;;) {
    if (deviceState == STATE_RUN) {
      MQTT.loop();
      Positioning.loop();
      Sync.loop(); // Auto-sync offline data saat online dan idle
      Storage.loopLogMaintenance();
      RFID.processDeferred();
      // Door.update() dipindah ke taskBuzzer agar tidak terblokir oleh operasi network
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskTime(void *pv) {
  for (;;) {
    if (deviceState == STATE_RUN) {
      Time.update();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void taskLCD(void *pv) {
  for (;;) {
    LCD.update();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void taskBuzzer(void *pv) {
  for (;;) {
    Buzzer.update();
    Door.update(); // Dipindah dari taskMQTT agar tidak terblokir operasi network
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void taskNotouch(void *pv) {
  const uint32_t sampleMs = 20;
  const uint8_t pressSamples = 5;   // ~100 ms LOW stabil sebelum trigger
  const uint8_t releaseSamples = 3; // ~60 ms HIGH stabil sebelum siap lagi

  uint8_t lowStreak = 0;
  uint8_t highStreak = 0;
  bool armed = true;

  while (true) {
    if (deviceState == STATE_RUN) {
      if (digitalRead(PIN_TOUCH_CONFIG) == LOW) {
        lowStreak++;
        highStreak = 0;

        if (armed && lowStreak >= pressSamples) {
          armed = false;
          lowStreak = 0;
          Door.noTouchOpen();
        }
      } else {
        highStreak++;
        lowStreak = 0;

        if (!armed && highStreak >= releaseSamples)
          armed = true;
      }
    } else {
      lowStreak = 0;
      highStreak = 0;
      armed = true;
    }

    vTaskDelay(pdMS_TO_TICKS(sampleMs));
  }
}

// void taskNotouch(void *parameter) {
//   while (true) {
//     if (deviceState == STATE_RUN) {
//       state_notouch = digitalRead(PIN_TOUCH_CONFIG);
//       if (state_notouch == 0) {
//         Buzzer.found();
//         Door.noTouchOpen();
//       }
//     }
//     vTaskDelay(pdMS_TO_TICKS(100));
//   }
// }

// ================= INIT FUNCTIONS =================

bool initStorage() {
  if (!SDCard_init())
    return false;

  if (!Storage.begin())
    return false;

  return true;
}

bool initConfig() {
  if (!Config.load())
    return false;

  Config.setDefaultIfInvalid();

  return true;
}

void startTasks() {
  xTaskCreate(taskTime, "time", 4096, NULL, 1, &taskHandleTime);
  xTaskCreate(taskRFID, "rfid", 8192, NULL, 3, &taskHandleRFID);
  xTaskCreate(taskMQTT, "mqtt", 8192, NULL, 2, &taskHandleMQTT);
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  safeBoot();
  LCD.begin();
  Buzzer.begin(PIN_BUZZER);
  Door.begin(PIN_RELAY);
  Time.begin();

  // Jalankan task LCD & Buzzer lebih awal agar bisa dipakai saat AP mode
  xTaskCreate(taskLCD, "lcd", 4096, NULL, 1, &taskHandleLCD);
  xTaskCreate(taskBuzzer, "buzzer", 2048, NULL, 2, &taskHandleBuzzer);
  //   xTaskCreate(taskNotouch, "notouch", 4096, NULL, 1, &taskHandleNotouch);
  xTaskCreate(taskNotouch, "notouch", 4096, NULL, 1, NULL);

  if (!initStorage()) {
    LCD.setInfo1("SD / Storage Error");
    delay(2000);
    ESP.restart();
  }

  if (!initConfig()) {
    deviceState = STATE_CONFIG;
    LCD.setInfo1("NOT CONFIGURED");
    LCD.setInfo2("Use Portal Mode");
    return;
  }

  Portal.beginApOnHold(PIN_CONFIG, 5000);

  if (Portal.isActive()) {
    deviceState = STATE_CONFIG;
    LCD.setInfo1("CONFIG MODE");
    LCD.setInfo2("Connect to WiFi");
    LCD.setStaticIp(WiFi.softAPIP().toString());
    return;
  }

  deviceState = STATE_RUN;
  Wifi.begin();
  MQTT.begin();
  Positioning.begin();
  Sync.begin(); // Init offline data queue
  RFID.begin(PIN_RFID_RX);

  startTasks();

  // Set waktu boot untuk tracking 10 detik pertama
  bootTimeMs = millis();
  lastApplyModeMs = bootTimeMs;

  Serial.println("System Ready");
  applyModeFromConfigToSystem();
}

// ================= LOOP =================

void loop() {
  if (deviceState == STATE_CONFIG) {
    Portal.loop();
    delay(10);
    return;
  }

  if (deviceState == STATE_RUN) {
    Wifi.handle();

    // Apply mode setiap 2 detik selama 10 detik pertama setelah boot
    unsigned long now = millis();
    unsigned long elapsedSinceBoot = now - bootTimeMs;

    if (elapsedSinceBoot < 10000) // 10 detik pertama
    {
      if (now - lastApplyModeMs >= 2000) // Setiap 2 detik
      {
        Serial.println("[Main] Re-apply mode (elapsed=" +
                       String(elapsedSinceBoot) + "ms)");
        applyModeFromConfigToSystem();
        lastApplyModeMs = now;
      }
    }
  }

  delay(1000);
}
