#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include "sdcard.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "rfid_manager.h"
#include "door_manager.h"
#include "lcd_manager.h"
#include "storage_manager.h"
#include "time_global.h"
#include "portal_config.h"
#include "buzzer_manager.h"
#include "positioning_manager.h"

#define PIN_RELAY 25
#define PIN_BUZZER 26
#define PIN_TOUCH_CONFIG 16
#define PIN_RFID_RX 4

// ================= DEVICE STATE =================
enum DeviceState
{
    STATE_BOOT,
    STATE_CONFIG,
    STATE_RUN
};

DeviceState deviceState = STATE_BOOT;

// ================= TASK HANDLES =================
TaskHandle_t taskHandleRFID;
TaskHandle_t taskHandleMQTT;
TaskHandle_t taskHandleTime;
TaskHandle_t taskHandleLCD;
TaskHandle_t taskHandleBuzzer;
TaskHandle_t taskHandleNotouch;

static void applyModeFromConfigToSystem()
{
    auto &cfg = Config.get();

    if (cfg.modeDeviceData == MODE_ACCESS_DOOR)
    {
        if (cfg.mode == MODE_OPEN)
        {
            Door.forceOpen();
            LCD.setInfo1("Force Open");
        }
        else if (cfg.mode == MODE_CLOSE)
        {
            Door.forceClose();
            LCD.setInfo1("Force Close");
        }
        else
        {
            Door.normal();
            LCD.setInfo1("");
        }

        return;
    }

    if (cfg.modeDeviceData == MODE_ATTENDANCE)
    {
        if (cfg.mode == MODE_ADD)
            LCD.setInfo1("ADD Card");
        else
            LCD.setInfo1("Scan");

        return;
    }
}

// ================= SAFE BOOT =================
void safeBoot()
{
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW); // RELAY SAFE STATE
    pinMode(PIN_TOUCH_CONFIG, INPUT_PULLUP);
}

// ================= TASKS =================

void taskRFID(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            RFID.loop();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void taskMQTT(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            MQTT.loop();
            Positioning.loop();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void taskTime(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            Time.update();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void taskLCD(void *pv)
{
    for (;;)
    {
        LCD.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void taskBuzzer(void *pv)
{
    for (;;)
    {
        Buzzer.update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void taskNotouch(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            if (digitalRead(PIN_TOUCH_CONFIG) == LOW)
            {
                Door.open();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ================= INIT FUNCTIONS =================

bool initStorage()
{
    if (!SDCard_init())
        return false;

    if (!Storage.begin())
        return false;

    return true;
}

bool initConfig()
{
    if (!Config.load())
        return false;

    return true;
}

void startTasks()
{
    xTaskCreate(taskLCD, "lcd", 4096, NULL, 1, &taskHandleLCD);
    xTaskCreate(taskBuzzer, "buzzer", 2048, NULL, 2, &taskHandleBuzzer);
    xTaskCreate(taskTime, "time", 4096, NULL, 1, &taskHandleTime);
    xTaskCreate(taskRFID, "rfid", 4096, NULL, 1, &taskHandleRFID);
    xTaskCreate(taskMQTT, "mqtt", 4096, NULL, 1, &taskHandleMQTT);
    xTaskCreate(taskNotouch, "notouch", 4096, NULL, 1, &taskHandleNotouch);
}

// ================= SETUP =================

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting...");

    safeBoot();
    LCD.begin();
    Buzzer.begin(PIN_BUZZER);
    Door.begin(PIN_RELAY);
    Time.begin();

    if (!initStorage())
    {
        LCD.setInfo1("SD / Storage Error");
        delay(2000);
        ESP.restart();
    }

    Portal.beginApOnHold(PIN_TOUCH_CONFIG, 10000);

    if (Portal.isActive())
    {
        deviceState = STATE_CONFIG;
        LCD.setInfo1("CONFIG MODE");
        LCD.setInfo2("Connect to WiFi");
        LCD.setStaticIp(WiFi.softAPIP().toString());
        return;
    }

    if (!initConfig())
    {
        deviceState = STATE_CONFIG;
        LCD.setInfo1("NOT CONFIGURED");
        LCD.setInfo2("Use Portal Mode");
        return;
    }

    deviceState = STATE_RUN;

    applyModeFromConfigToSystem();

    Wifi.begin();
    MQTT.begin();
    Positioning.begin();
    RFID.begin(PIN_RFID_RX);

    startTasks();

    Serial.println("System Ready");
}

// ================= LOOP =================

void loop()
{
    if (deviceState == STATE_CONFIG)
    {
        Portal.loop();
        delay(10);
        return;
    }

    if (deviceState == STATE_RUN)
    {
        Wifi.handle();
    }

    delay(1000);
}
