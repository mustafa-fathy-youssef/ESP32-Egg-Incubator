#include "incubator_internal.h"

// ========================================================================
// ESP32 Egg Incubator Controller - Professional Version
// ========================================================================
// الوظائف: 
//   - عد تنازلي للدورة (24 ساعة - FIX: كان التعليق يذكر خطأ "21 دقيقة"
//     رغم أن TARGET_SEC = 24*60*60 ثانية فعلياً)
//   - تحكم في السخان (Hysteresis Control)
//   - تحكم في المقلب (60s OFF / 15s ON)
//   - مروحة تعمل باستمرار
//   - قراءة حساسات DS18B20 (2 حساس)
//   - قراءة حساس DHT22 (رطوبة + حرارة)
//   - شاشة LCD (16x2)
//   - Watchdog Timer (8 ثوانٍ)
//   - حفظ في NVS (كل 10 ثوانٍ)
//   - Safe Mode (عند فشل الحساسات)
//   - Emergency Mode (عند ارتفاع الحرارة فوق 39.8°C)
//   - كشف عطل الريلاي (Relay Stuck)
//   - أمر Reset عبر Serial ("RESET")
// ========================================================================

#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
// #include <LiquidCrystal_I2C.h>
#include <LiquidCrystal_I2C.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <Arduino.h>
#include <Wire.h>
#include "diag_io.h"   // Phase 4+ : cloud I/O, predictive, cycle archive, Telegram
// #include "email_reporter.h"
#include "diagnostics.h"
#include "telegram_alerts.h"
// Firmware identity (Serial / support)
#define FIRMWARE_VERSION     "2.0.1"
#define FIRMWARE_NAME        "ESP32 Egg Incubator"

// ========================================================================
// 1. التعريفات الثابتة (Configuration)
// ========================================================================

// ---- الأزمنة ----
#define WDT_TIMEOUT         30      // Task WDT (s) — after safe reconfigure
#define SENSOR_INTERVAL      2000    // قراءة الحساسات كل 2 ثانية
#define TURNER_OFF_TIME      60000   // 60 ثانية واقف
#define TURNER_ON_TIME       15000   // 15 ثانية حركة
#define MAX_HEATER_RUNTIME   30000   // 30 ثا\\\نية كحد أقصى لتشغيل السخان
#define NVS_SAVE_INTERVAL    60      // حفظ في NVS كل 60 ثانية (FIX: كان التعليق يقول خطأ "10 ثوانٍ")

// ---- درجات الحرارة ----
#define TEMP_TARGET          37.7    // درجة الحرارة المستهدفة
#define TEMP_LOW             (TEMP_TARGET - 0.3)   // 37.2°C
#define TEMP_HIGH            (TEMP_TARGET + 0.1)   // 37.6°C
#define TEMP_CRITICAL        39.5    // حد الخطر (يطفئ السخان)
#define TEMP_EMERGENCY       39.8    // حد الطوارئ (يدخل Emergency Mode)
#define TEMP_SAFE_EXIT       TEMP_HIGH           // درجة الخروج من Emergency Mode
#define RELAY_STUCK_MARGIN   2.0     // هامش كشف عطل الريلاي

// ---- الـ GPIO Pins ----
#define PIN_ONE_WIRE         4       // DS18B20
#define PIN_DHT              15      // DHT22
#define PIN_FAN              13      // المروحة
#define PIN_TURNER           14      // المقلب
#define PIN_HEATER           27      // السخان
#define PIN_SENSOR_FAILED    5       // LED عطل الحساسات
// #define TARGET_SEC (24UL * 60UL * 60UL)   // 86400 ثانية

// ---- أنواع الحساسات ----
#define DHT_TYPE             DHT22

// ---- عدد الحساسات (قابل للتوسع) ----
#define MAX_DS18B20_SENSORS  2       // حالياً 2 حساس (يمكن زيادتها لاحقاً)

// ---- Temperature Smoothing (EMA) ----
#define TEMP_SMOOTHING_ALPHA  0.25   // 0.1=بطيء (ثبات عالي) .. 0.5=سريع
float rawTemperature = 0;            // القيمة الخام - تُستخدم في فحوصات الطوارئ
bool  smootherInitialized = false;

#define EMERGENCY_EXIT_STABLE_TIME  10000       // لازم تفضل مستقرة 10 ثواني قبل الخروج
// ========================================================================
// 2. تعريف الهياكل (Structs) لإدارة البيانات
// ========================================================================
// ---- إعادة محاولة DHT التلقائية ----
#define DHT_RETRY_INTERVAL  60000   // 1  

#define TEMP_ABSOLUTE_MAX TEMP_EMERGENCY  // absolute cutoff = emergency threshold (single source)


// ============================================================
// Dynamic Configuration (NVS)
// ============================================================
struct SystemConfig {
    float targetTemp;           // 37.7
    float tempLow;              // target - 0.3
    float tempHigh;             // target + 0.1
    float tempCritical;         // 39.5
    float tempEmergency;        // 39.8
    unsigned long turnerOffTime; // 60000
    unsigned long turnerOnTime;  // 15000
    unsigned long heaterMaxRuntime; // 30000
    unsigned long nvsSaveInterval;  // 60 (ثواني)
};

SystemConfig config;
Preferences prefConfig;

// قيم افتراضية (تُستخدم عند أول تشغيل)

void loadConfig() {
    prefConfig.begin("inc_config", true); // read-only أولاً

    // إذا لم تكن المساحة موجودة → قيم افتراضية
    if (!prefConfig.isKey("targetTemp")) {
        prefConfig.end();
        setDefaultConfig();
        saveConfig(); // حفظ القيم الافتراضية مرة واحدة
        Serial.println("⚙️ Config: defaults loaded & saved");
        return;
    }

    config.targetTemp       = prefConfig.getFloat("targetTemp", 37.7f);
    config.tempLow          = prefConfig.getFloat("tempLow", 37.4f);
    config.tempHigh         = prefConfig.getFloat("tempHigh", 37.8f);
    config.tempCritical     = prefConfig.getFloat("tempCritical", 39.5f);
    config.tempEmergency    = prefConfig.getFloat("tempEmergency", 39.8f);
    config.turnerOffTime    = prefConfig.getULong("turnerOff", 60000UL);
    config.turnerOnTime     = prefConfig.getULong("turnerOn", 15000UL);
    config.heaterMaxRuntime = prefConfig.getULong("heaterMax", 30000UL);
    config.nvsSaveInterval  = prefConfig.getULong("nvsSave", 60UL);

    prefConfig.end();
    Serial.println("⚙️ Config loaded from NVS");
}

void saveConfig() {
    prefConfig.begin("inc_config", false);
    prefConfig.putFloat("targetTemp", config.targetTemp);
    prefConfig.putFloat("tempLow", config.tempLow);
    prefConfig.putFloat("tempHigh", config.tempHigh);
    prefConfig.putFloat("tempCritical", config.tempCritical);
    prefConfig.putFloat("tempEmergency", config.tempEmergency);
    prefConfig.putULong("turnerOff", config.turnerOffTime);
    prefConfig.putULong("turnerOn", config.turnerOnTime);
    prefConfig.putULong("heaterMax", config.heaterMaxRuntime);
    prefConfig.putULong("nvsSave", config.nvsSaveInterval);
    prefConfig.end();
    Serial.println("💾 Config saved to NVS");
}
// ========================================================================
// 5. دوال مساعدة (Helper Functions)
// ========================================================================

// ---- تشغيل/إيقاف مشغل ----

void updateTimer() {
    unsigned long now = millis();
    static unsigned long remainderMs = 0;
    unsigned long diff = now - lastMillis;
    remainderMs += diff;
    lastMillis = now;

    unsigned long deltaSec = remainderMs / 1000;
    remainderMs %= 1000;

    if (deltaSec > 0 && elapsedSec < TARGET_SEC) {
        elapsedSec += deltaSec;
        
        // FIX: استخدام الإعداد الديناميكي بدلاً من #define
        if (elapsedSec % config.nvsSaveInterval == 0) {
            pref.putULong("elapsed_sec", elapsedSec);
            Serial.println("💾 Saved to NVS");
        }
        
        // عند الوصول للهدف
        if (elapsedSec >= TARGET_SEC) {
            elapsedSec = 0;
            remainderMs = 0;
            lastMillis = millis();
            cycleCount++;
            pref.putULong("elapsed_sec", 0);
            pref.putULong("cycle_count", cycleCount);
            Serial.printf("✅ Cycle #%lu completed!\n", cycleCount);
            { char logBuf[48]; snprintf(logBuf, sizeof(logBuf), "Cycle #%lu completed", cycleCount); logEvent(logBuf); }

            {
                float avgTemp = (cycle_sample_count > 0)
                    ? (cycle_temp_sum / (float)cycle_sample_count)
                    : currentTemperature;
                cycle_archive_onCycleComplete(cycleCount, getHealthScore(),
                                              avgTemp, getCycleDutyCycle(), getCycleAvgOvershoot());
            }
            cycle_temp_sum = 0.0f;
            cycle_sample_count = 0;
            diag_cycleReset();
        }
    }
}
// ========================================================================
// 13. دالة معالجة أوامر Serial
// ========================================================================

void setup() {
    Serial.begin(115200);
    // Serial.println("Working");
    delay(50);
    Wire.setTimeout(50);

    // 1) Watchdog FIRST — prevents "task not found" and failed mid-setup init
    setupWatchdog();
    esp_task_wdt_reset();

    // ---- Preferences ----
    pref.begin("inc_timer", false);
        // ---- تحميل الإعدادات الديناميكية ----
    loadConfig();

    // NTP optional (fails offline quickly). Only after WDT is registered.
    // if (syncTime()) {
    //     Serial.println(String("✅ Time synchronized: ") + getTimeString());
    // } else {
    //     Serial.println("⚠️ Failed to sync time. Using default.");
    // }
    esp_task_wdt_reset();

    // ---- قراءة وزيادة bootCount ----
    bootCount = pref.getUChar("bootCount", 0);
    bootCount++;
    pref.putUChar("bootCount", bootCount);
    Serial.printf("Boot #%d\n", bootCount);

    Serial.println("System Starting...");
    Serial.printf("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    lastMillis = millis();
    Serial.printf("🔍 Last reset reason raw code: %d\n", esp_reset_reason());

    // ---- Restore safety / timer state from NVS ----
    elapsedSec = pref.getULong("elapsed_sec", 0);
    cycleCount = pref.getULong("cycle_count", 0);
    safeMode = pref.getBool("safe_mode", false);
    emergencyMode = pref.getBool("emergency", false);

    if (elapsedSec >= TARGET_SEC) {
        elapsedSec = 0;
        pref.putULong("elapsed_sec", 0);
        Serial.println("⚠️ Invalid NVS value, reset to 0");
    }

    // WDT already configured in setupWatchdog() — do NOT call esp_task_wdt_init again
    esp_task_wdt_reset();

// ---- تهيئة المشغلات ----
    for (int i = 0; i < ACTUATOR_COUNT; i++) {
        pinMode(actuators[i].pin, OUTPUT);
        digitalWrite(actuators[i].pin, LOW);
        actuators[i].state = false;
    }
    pinMode(PIN_SENSOR_FAILED, OUTPUT);
    digitalWrite(PIN_SENSOR_FAILED, LOW);
    
    // ---- تهيئة الحساسات ----
    initSensors();
    
    // ---- تهيئة الشاشة ----
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready!");
    {
        unsigned long t0 = millis();
        while ((millis() - t0) < 1000UL) {
            esp_task_wdt_reset();
            delay(10);
            esp_task_wdt_reset();  // بعد كل delay طويل

        }
    }
    lcd.clear();    
    lcdInitialized = true;
    
    // ---- استعادة حالة السلامة ----
    if (safeMode) {
        Serial.println("⚠️ System starting in SAFE MODE");
        setActuator(0, true);
        setActuator(1, false);
        setActuator(2, false);
    }
    
    if (emergencyMode) {
        Serial.println("🚨 System starting in EMERGENCY MODE");
        setActuator(0, true);
        setActuator(1, false);
        setActuator(2, false);
    }
    
    logEvent("System started");
    
    Serial.println("✅ System Ready!");
    Serial.printf("🧠 Free heap at boot: %u bytes\n", (unsigned)ESP.getFreeHeap());
    diag_init();
    diag_io_init();
    
    // Boot diagnostics log (NVS) + optional Telegram if online
    update_last_reset_reason();
    esp_task_wdt_reset();

    // DetailedLog log;
    // log.id = nextLogId;
    // log.type = LOG_TYPE_RESET;
    // snprintf(log.reason, sizeof(log.reason), "%s", last_reset_reason);
    // {
    //     String ts = getTimeString();
    //     snprintf(log.timestamp, sizeof(log.timestamp), "%s", ts.c_str());
    // }
    // log.uptimeSec = millis() / 1000UL;
    // log.value = 0.0f;
    // log.code = 0;
    // saveLog(log);

    // Serial.println("===== BOOT LOG =====");
    // Serial.printf("Time: %s", log.timestamp);
    // Serial.printf("Uptime: %lu sec", log.uptimeSec);
    // Serial.printf("Reason: %s", log.reason);

    if (wifiConnected) {
        char bootMsg[200];
        // snprintf(bootMsg, sizeof(bootMsg),
                //  "Boot Report Time: %s Uptime: %lu s Reason: %s",
                //  log.timestamp, log.uptimeSec, log.reason);
        // snprintf(bootMsg, sizeof(bootMsg),
            //  "Boot Report Time: %s Uptime: %lu s Reason: %s",
            //  log.timestamp, log.uptimeSec, log.reason);

        esp_task_wdt_reset();
        tgSend(bootMsg);
        esp_task_wdt_reset();
    } else {
        Serial.println("📡 Boot TG skipped (offline)");
    }

    esp_task_wdt_reset();
}

// ========================================================================
// 15. دالة loop()
// ========================================================================

// ============================================================
// Finite State Machine – Safety-Critical Design
// ESP32 Egg Incubator – Industrial Grade
// ============================================================



// ============================================================
// loop() – Precision Timing + Safety-Critical FSM
// ============================================================
void loop() {
    // --------------------------------------------------------
    // 1. Watchdog – أول سطر بدون أي شرط (أمان أقصى)
    // --------------------------------------------------------
    esp_task_wdt_reset();

    const unsigned long now = millis();

    // --------------------------------------------------------
    // 2. مهام فائقة السرعة (كل دورة ≈ 1–5 ms)
    // --------------------------------------------------------
    handleSerialCommands();

    if (waitingForConfirm) {
        if (Serial.available()) {
            char confirm[16] = {0};
            size_t n = 0;
            while (Serial.available() && n < sizeof(confirm) - 1) {
                char ch = Serial.read();
                if (ch == '\n' || ch == '\r') break;
                confirm[n++] = ch;
            }
            if (strcmp(confirm, "CONFIRM") == 0 || strcmp(confirm, "confirm") == 0) {
                clearAllDiagnostics();
                Serial.println("✅ All diagnostics reset to zero.");
                waitingForConfirm = false;
            } else {
                Serial.println("❌ Confirmation cancelled.");
                waitingForConfirm = false;
            }
        } else if ((int32_t)(now - confirmTimeout) >= 0) {
            Serial.println("❌ Confirmation timeout.");
            waitingForConfirm = false;
        }
    }

    // --------------------------------------------------------
    // 3. المروحة دائمًا شغالة في كل الحالات (Failsafe)
    // --------------------------------------------------------
    if (!actuators[0].state) {
        setActuator(0, true);
    }

    // --------------------------------------------------------
    // 4. Hardware Fallback للسخان
    // FIX: يبقى الشرط شاملاً currentState أو emergencyMode لمنع أي نافذة زمنية
    // --------------------------------------------------------
    if (currentState == STATE_EMERGENCY || emergencyMode) {
        if (actuators[2].state || heaterState) {
            digitalWrite(PIN_HEATER, LOW);
            actuators[2].state = false;
            heaterState = false;
        }
    }

    // --------------------------------------------------------
    // 5. المهمة المتوسطة – كل 2 ثانية
    //    قراءة الحساسات + LCD + تشخيص تعمل في كل الحالات
    // --------------------------------------------------------
    static unsigned long lastSensorTime = 0;
    if (now - lastSensorTime >= SENSOR_INTERVAL) {
        lastSensorTime = now;

        // قراءة الحساسات تعمل في كل الحالات
        readSensors();

        // Automatic exit check for Safe and Emergency modes
        checkAutomaticSafetyExit();

        // ----------------------------------------------------
        // التحكم الخطير فقط في STATE_NORMAL
        // ----------------------------------------------------
        if (currentState == STATE_NORMAL) {
            controlHeater();

            // المقلب
                        // المقلب – يستخدم الإعدادات الديناميكية
            if (!isPredictiveModeActive()) {
                if (!turnerState) {
                    if (now - lastTurnerTime >= config.turnerOffTime) {
                        setActuator(1, true);
                        turnerState = true;
                        lastTurnerTime = now;
                        Serial.println("Turner ON");
                    }
                } else {
                    if (now - lastTurnerTime >= config.turnerOnTime) {
                        setActuator(1, false);
                        turnerState = false;
                        lastTurnerTime = now;
                        Serial.println("Turner OFF");
                    }
                }
            }

            // FIX: مراقبة وقت السخان وكشف التصاق الريلاي فقط في التشغيل الطبيعي
            checkHeaterRuntime();
            checkRelayStuckFault();

            // FIX: العداد التنازلي يعمل فقط أثناء التشغيل الطبيعي
            updateTimer();
        }

        // FIX: تأكيد إطفاء المقلب قسراً في SAFE و EMERGENCY
        if (currentState != STATE_NORMAL) {
            if (turnerState || actuators[1].state) {
                setActuator(1, false);
                turnerState = false;
            }
        }

        // المرطب: يعمل في NORMAL و SAFE، ويُطفأ في EMERGENCY
        if (currentState != STATE_EMERGENCY) {
            controlEvaporator();
        } else {
            // FIX: تأكيد إطفاء المرطب فوراً في الطوارئ (Hardware + Software)
            if (evaporator.state || actuators[3].state) {
                evaporator.state = false;
                setActuator(3, false);
            }
        }

        // التشخيص وقراءة البيانات تعمل في كل الحالات
        diag_sample();

        if (hasValidTemperature) {
            cycle_temp_sum += currentTemperature;
            cycle_sample_count++;
        }

        // LED عطل الحساسات
        bool allSensorsFailed = true;
        for (int i = 0; i < MAX_DS18B20_SENSORS; i++) {
            if (dsSensors[i].valid) {
                allSensorsFailed = false;
                break;
            }
        }
        digitalWrite(PIN_SENSOR_FAILED, allSensorsFailed ? HIGH : LOW);

        updateLCD();

        // طباعة حالة مختصرة (throttle) – تبقى خارج الـ switch
        static uint8_t statusPrintSkip = 0;
        if ((++statusPrintSkip & 1) == 0) {
            float hum = 0, dhtTemp = 0;
            for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
                if (humiditySensors[i].valid && !humiditySensors[i].bypassed) {
                    hum = humiditySensors[i].humidity;
                    dhtTemp = humiditySensors[i].temperature;
                    break;
                }
            }
            Serial.printf("T1: %.2f | T2: %.2f | Avg: %.1f | DHT: %.1f | Hum: %.1f | Heater: %s | Turner: %s | State: %s\n",
                          dsSensors[0].temperature,
                          dsSensors[1].temperature,
                          currentTemperature,
                          dhtTemp,
                          hum,
                          heaterState ? "ON" : "OFF",
                          turnerState ? "ON" : "OFF",
                          currentStateName);
        }
    }

    // --------------------------------------------------------
    // 6. المهمة البطيئة – كل 10 ثوانٍ
    // --------------------------------------------------------
    static unsigned long lastDiagPeriodic = 0;
    if ((uint32_t)(now - lastDiagPeriodic) >= 10000UL) {
        lastDiagPeriodic = now;
        diag_periodic();
    }

    // --------------------------------------------------------
    // 7. المهمة الخلفية – كل 100 مللي
    // --------------------------------------------------------
    static unsigned long lastIoLoop = 0;
    if ((uint32_t)(now - lastIoLoop) >= 100UL) {
        lastIoLoop = now;
        esp_task_wdt_reset();
        diag_io_loop();
        telegram_monitor_loop();
        esp_task_wdt_reset();
    }

    // --------------------------------------------------------
    // 8. بوابة أوامر التيليجرام (كل 3 ثوانٍ)
    // --------------------------------------------------------
    static unsigned long lastTelegramPoll = 0;
    if (wifiConnected && (now - lastTelegramPoll >= 3000UL)) {
        lastTelegramPoll = now;
        esp_task_wdt_reset();
        checkTelegramCommands();
        esp_task_wdt_reset();
    }

    // --------------------------------------------------------
    // 9. فحص LCD كل 5 ثوانٍ
    // --------------------------------------------------------
    static unsigned long lastLCDCheck = 0;
    if (now - lastLCDCheck >= 5000UL) {
        lastLCDCheck = now;
        Wire.beginTransmission(0x27);
        if (Wire.endTransmission() != 0) {
            if (lcdInitialized) {
                Serial.println("⚠️ LCD lost connection");
                lcdInitialized = false;
            }
        } else if (!lcdInitialized) {
            reinitLCD();
        }
    }

    // ============================================================
    // 10. آلة الحالات – الانتقالات
    // ============================================================
    switch (currentState) {
        case STATE_BOOT:
            if (emergencyMode) {
                changeState(STATE_EMERGENCY);
            } else if (safeMode) {
                changeState(STATE_SAFE);
            } else {
                changeState(STATE_NORMAL);
            }
            break;

        case STATE_NORMAL:
            if (emergencyMode) {
                changeState(STATE_EMERGENCY);
            } else if (safeMode) {
                changeState(STATE_SAFE);
            }
            break;

        case STATE_SAFE:
            if (emergencyMode) {
                changeState(STATE_EMERGENCY);
            } else if (!safeMode) {
                changeState(STATE_NORMAL);
            }
            break;

        case STATE_EMERGENCY:
            // منطقة ميتة 10 ثوانٍ تتم داخل exitEmergencyMode()
            if (!emergencyMode) {
                if (safeMode) {
                    changeState(STATE_SAFE);
                } else {
                    changeState(STATE_NORMAL);
                }
            }
            break;
    }
}