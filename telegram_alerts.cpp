#include "telegram_internal.h"

// telegram_alerts.cpp
// Phase 4B – Non-blocking Telegram alerts with multi-code debounce
// Credentials: secrets.h (never commit real values)

#include "telegram_alerts.h"
#include "diagnostics.h"
#include "secrets.h"
#include <Preferences.h>
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <stdio.h>
#include <string.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "diag_io.h"   
#include <time.h>   
#define MAX_LOGS 50



struct ResetStats {
    uint32_t totalCount;
    uint32_t lastCount;
    uint32_t lastReasonCode;
    unsigned long lastBootTime;
    unsigned long lastDay;
    uint32_t dailyCount;
    uint32_t wdtCount, panicCount, brownoutCount, poweronCount, otherCount;
};

WiFiClientSecure secured_client;
UniversalTelegramBot bot(SECRET_TG_BOT_TOKEN, secured_client);
Preferences prefReset;
Preferences prefLog;    
ResetStats resetStats;

DetailedLog logBuffer[MAX_LOGS];
uint32_t logCount = 0;
uint32_t nextLogId = 1;

const char* ntpServer = "time.google.com";
const long  gmtOffset_sec = 7200;   // +2 ساعة (توقيت مصر الصيفي قد يكون 3، نضبط حسب الموسم)
const int   daylightOffset_sec = 0; // 3600 لو الصيفي

time_t nowTime;
struct tm timeinfo;

// ---- المتغيرات العامة المطلوبة للأوامر الجديدة ----
extern unsigned long elapsedSec;
// extern unsigned long TARGET_SEC;
extern unsigned long cycleCount;
extern float currentTemperature;
extern float avgHumidity;
extern bool heaterState;
extern bool turnerState;
extern bool safeMode;
extern bool emergencyMode;
extern bool hasValidTemperature;
extern uint32_t sensorDisconnectCount;
extern DSTemperatureSensor dsSensors[];
extern Actuator actuators[];
extern uint8_t bootCount;
// إعلان دوال من telegram_alerts.cpp
extern void saveLog(const DetailedLog& log);
extern String getTimeString();

// لتخزين آخر سبب لإعادة التشغيل ليتم إرساله عند الطلب
char last_reset_reason[80] = "Unknown";  // M6: هامش UTF-8 عربي

static const char* TG_BOT_TOKEN = SECRET_TG_BOT_TOKEN;
static const char* TG_CHAT_ID   = SECRET_TG_CHAT_ID;
static const unsigned long TG_TIMEOUT_MS = 2500UL;
static const unsigned long DEBOUNCE_MS   = 3600000UL;

#define DEBOUNCE_RING 4
static uint8_t  recentCodes[DEBOUNCE_RING] = {0xFF, 0xFF, 0xFF, 0xFF};
static unsigned long recentTimes[DEBOUNCE_RING] = {0};
static uint8_t  recentIdx = 0;

static bool isDebounced(uint8_t code) {
    unsigned long now = millis();
    for (uint8_t i = 0; i < DEBOUNCE_RING; i++) {
        if (recentCodes[i] == code && (now - recentTimes[i]) < DEBOUNCE_MS)
            return true;
    }
    return false;
}

static void recordSent(uint8_t code) {
    recentCodes[recentIdx] = code;
    recentTimes[recentIdx] = millis();
    recentIdx = (recentIdx + 1) % DEBOUNCE_RING;
}

static bool lastEmergState = false;
static unsigned long lastDailyReportDay = 0;
static char msgBuf[180];

extern bool emergencyMode;
extern unsigned long cycleCount;
extern float rawTemperature;

// ---- متغير لإرسال تقرير الإقلاع التلقائي (مرة واحدة فقط، غير حاجب) ----
static bool bootReportSent = false;

// ---- Strip characters that would break our simple JSON body ----
static void sanitizeTgText(char* s, size_t cap) {
    if (!s) return;
    for (size_t i = 0; s[i] && i < cap; i++) {
        if (s[i] == '"' || s[i] == '\\' || s[i] == '\n' || s[i] == '\r')
            s[i] = ' ';
    }
}

// ---- Single HTTP attempt; used by tgSend with one retry ----
static bool tgSendOnce(const char* text) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ Telegram FAIL: WiFi NOT connected!");
        return false;
    }
    
    char url[128];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot%s/sendMessage", TG_BOT_TOKEN);
             
    char body[700];  
    snprintf(body, sizeof(body),
             "{\"chat_id\":\"%s\",\"text\":\"%s\"}", TG_CHAT_ID, text);
             
    HTTPClient http;
    http.setTimeout(800);  // C6: مهلة أقصر عند الإقلاع/الشبكة الضعيفة
    esp_task_wdt_reset();   
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    esp_task_wdt_reset();   
    
    int code = http.POST(body);
    http.end();
    esp_task_wdt_reset();   
    
    Serial.printf("📡 Telegram HTTP Code: %d\n", code);
    return (code >= 200 && code < 300);
}

void telegram_init() {
    initLogStorage();
    for (uint8_t i = 0; i < DEBOUNCE_RING; i++) {
        recentCodes[i] = 0xFF;
        recentTimes[i] = 0;
    }
    secured_client.setInsecure();
    secured_client.setTimeout(2);  // C3
    recentIdx = 0;
    lastEmergState = false;
    lastDailyReportDay = 0;
    
    updateResetStats();
    
    // محاولة إرسال تقرير الإقلاع التلقائي (مرة واحدة، غير حاجبة)
    sendBootResetReport();
    
    Serial.println("📲 Telegram alerts ready (multi-code debounce)");
}

void telegram_send_fault(uint8_t code, uint8_t level, float value) {
    if (isDebounced(code)) return;
    unsigned long now = millis();
    unsigned long uptimeSec = now / 1000UL;
    unsigned long days  = uptimeSec / 86400UL;
    unsigned long hours = (uptimeSec % 86400UL) / 3600UL;
    snprintf(msgBuf, sizeof(msgBuf),
             "INCUBATOR CRITICAL! Code: 0x%02X, Value: %.1f, Uptime: %lud %luh",
             code, value, days, hours);
    sanitizeTgText(msgBuf, sizeof(msgBuf));
    if (tgSend(msgBuf)) {
        recordSent(code);
        Serial.println("📲 Telegram alert sent");
    }
}

void telegram_send_daily_report() {
    float score = getHealthScore();
    const char* cls = getHealthClass();
    snprintf(msgBuf, sizeof(msgBuf),
             "Daily Report Health: %.1f (%s) Cycles: %lu",
             score, cls, cycleCount);
    sanitizeTgText(msgBuf, sizeof(msgBuf));
    if (tgSend(msgBuf))
        Serial.println("📲 Daily Telegram report sent");
}

// ============================================================
// وضع المراقبة المستمرة (كل دقيقة)
// ============================================================
bool monitorEnabled = false;  // C5: افتراضي OFF — التفعيل بـ MONITORON فقط
unsigned long lastMonitorMs = 0;

void telegram_send_periodic_report() {
    extern float currentTemperature;
    extern bool heaterState;
    extern bool turnerState;
    extern bool emergencyMode;
    extern bool safeMode;
    extern unsigned long cycleCount;
    extern float avgHumidity;
    extern DSTemperatureSensor dsSensors[];

    char buffer[512];
    snprintf(buffer, sizeof(buffer),
        "📊 *تقرير الحاضنة*\n"
        "=====================\n"
        "🌡️ الحرارة:\n"
        "   T1: %.2f°C | T2: %.2f°C\n"
        "   المتوسط: %.1f°C\n"
        "💧 الرطوبة: %.1f%%\n"
        "🔥 السخان: %s\n"
        "🔄 المقلب: %s\n"
        "⏱️ وقت التشغيل: %lu ثانية\n"
        "📈 عدد الدورات: %lu\n"
        "🩺 الصحة: %.1f/100\n"
        "⚠️ الحالة: %s",
        dsSensors[0].temperature,
        dsSensors[1].temperature,
        currentTemperature,
        avgHumidity,
        heaterState ? "ON" : "OFF",
        turnerState ? "ON" : "OFF",
        millis() / 1000,
        cycleCount,
        getHealthScore(),
        safeMode ? "SAFE MODE" : (emergencyMode ? "EMERGENCY" : "NORMAL")
    );

    if (tgSend(buffer)) {
        Serial.println("📊 Periodic Telegram report sent successfully!");
    } else {
        Serial.println("❌ Periodic Telegram report FAILED to send.");
    }
}
