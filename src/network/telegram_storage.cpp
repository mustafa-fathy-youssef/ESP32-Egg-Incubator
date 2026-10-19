#include "telegram_internal.h"

void initLogStorage() {
    prefLog.begin("logs", false);
    logCount = prefLog.getULong("logCount", 0);
    nextLogId = prefLog.getULong("nextId", 1);
    // قراءة السجلات المحفوظة
    if (logCount > MAX_LOGS) logCount = MAX_LOGS;
    for (uint32_t i = 0; i < logCount; i++) {
        char key[20];
        snprintf(key, sizeof(key), "log%lu", i);
        prefLog.getBytes(key, (uint8_t*)&logBuffer[i], sizeof(DetailedLog));
    }
}

void saveLog(const DetailedLog& log) {
    uint32_t idx = logCount % MAX_LOGS;
    logBuffer[idx] = log;
    logCount++;
    if (logCount > MAX_LOGS) logCount = MAX_LOGS; // نحدها
    prefLog.putULong("logCount", logCount);
    prefLog.putULong("nextId", nextLogId);
    char key[20];
    snprintf(key, sizeof(key), "log%lu", idx);
    prefLog.putBytes(key, (uint8_t*)&log, sizeof(DetailedLog));
    nextLogId++;
    prefLog.putULong("nextId", nextLogId);
}


void initResetStats() {
    prefReset.begin("reset_stats", false);
    resetStats.totalCount = prefReset.getULong("total", 0);
    resetStats.lastCount = prefReset.getULong("last_count", 0);
    resetStats.lastReasonCode = prefReset.getULong("last_reason", 0);
    resetStats.lastBootTime = prefReset.getULong("last_boot", 0);
    resetStats.lastDay = prefReset.getULong("last_day", 0);
    resetStats.dailyCount = prefReset.getULong("daily", 0);
    resetStats.wdtCount = prefReset.getULong("wdt", 0);
    resetStats.panicCount = prefReset.getULong("panic", 0);
    resetStats.brownoutCount = prefReset.getULong("brown", 0);
    resetStats.poweronCount = prefReset.getULong("power", 0);
    resetStats.otherCount = prefReset.getULong("other", 0);
}

void saveResetStats() {
    prefReset.putULong("total", resetStats.totalCount);
    prefReset.putULong("last_count", resetStats.lastCount);
    prefReset.putULong("last_reason", resetStats.lastReasonCode);
    prefReset.putULong("last_boot", resetStats.lastBootTime);
    prefReset.putULong("last_day", resetStats.lastDay);
    prefReset.putULong("daily", resetStats.dailyCount);
    prefReset.putULong("wdt", resetStats.wdtCount);
    prefReset.putULong("panic", resetStats.panicCount);
    prefReset.putULong("brown", resetStats.brownoutCount);
    prefReset.putULong("power", resetStats.poweronCount);
    prefReset.putULong("other", resetStats.otherCount);
}

void updateResetStats() {
    initResetStats();
    
    esp_reset_reason_t reason = esp_reset_reason();
    uint32_t code = 0;
    
    switch (reason) {
        case ESP_RST_WDT: code = 1; resetStats.wdtCount++; break;
        case ESP_RST_INT_WDT: code = 2; resetStats.wdtCount++; break;
        case ESP_RST_PANIC: code = 3; resetStats.panicCount++; break;
        case ESP_RST_BROWNOUT: code = 4; resetStats.brownoutCount++; break;
        case ESP_RST_POWERON: code = 5; resetStats.poweronCount++; break;
        case ESP_RST_SW: code = 7; resetStats.otherCount++; break;
        case ESP_RST_TASK_WDT: code = 8; resetStats.wdtCount++; break;
        case ESP_RST_PWR_GLITCH: code = 9; resetStats.brownoutCount++; break;
        case ESP_RST_USB: code = 10; resetStats.otherCount++; break;
        case ESP_RST_DEEPSLEEP: code = 11; resetStats.otherCount++; break;
        case ESP_RST_EXT: code = 12; resetStats.otherCount++; break;
        case ESP_RST_CPU_LOCKUP: code = 13; resetStats.otherCount++; break;
        case ESP_RST_SDIO: code = 14; resetStats.otherCount++; break;
        case ESP_RST_JTAG: code = 15; resetStats.otherCount++; break;
        case ESP_RST_EFUSE: code = 16; resetStats.otherCount++; break;
        default: code = 6; resetStats.otherCount++; break;
    }       
    
    resetStats.totalCount++;
    resetStats.lastReasonCode = code;
    resetStats.lastCount++;
    
    unsigned long currentDay = millis() / 86400000UL;
    if (currentDay != resetStats.lastDay) {
        resetStats.lastDay = currentDay;
        resetStats.dailyCount = 1;
    } else {
        resetStats.dailyCount++;
    }
    
    resetStats.lastBootTime = millis();
    saveResetStats();
}

// ============================================================
// إرسال تقرير الإقلاع التلقائي (مرة واحدة، غير حاجب)
// ============================================================
static void sendBootResetReport() {
    // C6: غير حاجب — يُعاد لاحقاً عند فشل الشبكة
    if (bootReportSent) return;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("📡 Boot report pending: WiFi not connected yet.");
        return;
    }
    char report[768];
    fillDetailedResetReport(report, sizeof(report));
    if (tgSend(report)) {
        bootReportSent = true;
        Serial.println("📲 Boot reset report sent automatically.");
    } else {
        Serial.println("❌ Failed to send boot reset report.");
    }
}

// ============================================================
// دوال التيليجرام العامة
