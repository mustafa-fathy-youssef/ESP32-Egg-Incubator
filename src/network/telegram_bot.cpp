#include "telegram_internal.h"

bool tgSend(const char* text) {
    if (tgSendOnce(text)) return true;
    {
        unsigned long t0 = millis();
        while ((millis() - t0) < 100UL) {
            esp_task_wdt_reset();
            delay(1);
        }
    }
    return tgSendOnce(text);
}

// ============================================================
// إحصائيات أسباب إعادة التشغيل (Reset Statistics)
// ============================================================
bool syncTime() {
    // setupWatchdog() already registered loopTask — reset is safe here.
    // Keep short retries: offline boards must not stall setup.
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    int retries = 0;
    while (retries < 3 && !getLocalTime(&timeinfo)) {
        delay(150);
        retries++;
        esp_task_wdt_reset();
    }
    return (retries < 6);
}

// دالة للحصول على String بالتنسيق "YYYY-MM-DD HH:MM:SS"
String getTimeString() {
    getLocalTime(&timeinfo);
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buf);
}


void telegram_set_monitoring(bool enabled) {
    monitorEnabled = enabled;
    Serial.printf("📊 Periodic Telegram Monitoring: %s\n", enabled ? "ON" : "OFF");
}

void telegram_monitor_loop() {
    if (monitorEnabled) {
        if (millis() - lastMonitorMs >= 300000UL) {  // C5: 5 دقائق بدل 60 ثانية
            lastMonitorMs = millis();
            telegram_send_periodic_report();
        }
    }
}

void telegram_loop() {
    if (emergencyMode != lastEmergState) {
        if (emergencyMode)
            telegram_send_fault(FAULT_EMERGENCY_TEMP, FAULT_EMERGENCY, rawTemperature);
        lastEmergState = emergencyMode;
    }
    unsigned long day = millis() / 86400000UL;
    if (day != lastDailyReportDay && day > 0) {
        lastDailyReportDay = day;
        telegram_send_daily_report();
    }
}
void resetTelegramBot() {
    // W6 + C3: إعادة تهيئة كاملة + مهلة عميل
    secured_client.setInsecure();
    secured_client.setTimeout(2);  // ثوانٍ — يقلل احتمال حجب TLS الطويل
    bootReportSent = false;
    lastEmergState = false;
    lastDailyReportDay = 0;
    memset(recentCodes, 0xFF, sizeof(recentCodes));
    memset(recentTimes, 0, sizeof(recentTimes));
    recentIdx = 0;
    Serial.println("🔄 Telegram bot reinitialized after WiFi recovery");
}
// ============================================================
// دوال أسباب إعادة التشغيل
// ============================================================
// ============================================================
// بناء تقرير أسباب إعادة التشغيل بتنسيق جميل (بديل fillDetailedResetReport)

void checkTelegramCommands() {
    // C1/C2/C3: حارس WiFi + سقف رسائل + مهلة drain — لا تجميد loop
    if (WiFi.status() != WL_CONNECTED) return;

    esp_task_wdt_reset();
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    esp_task_wdt_reset();

    const int MAX_MESSAGES_PER_CALL = 2;
    const unsigned long MAX_DRAIN_MS = 800UL;
    unsigned long t0 = millis();
    int processed = 0;
    String reply;

    while (numNewMessages && processed < MAX_MESSAGES_PER_CALL
           && (millis() - t0) < MAX_DRAIN_MS) {
        esp_task_wdt_reset();
        for (int i = 0; i < numNewMessages && processed < MAX_MESSAGES_PER_CALL; i++) {
            if (bot.messages[i].chat_id != SECRET_TG_CHAT_ID) {
                processed++;
                continue;
            }
            String text = bot.messages[i].text;
            processed++;

            // ---- الأوامر القديمة ----
            if (text == "/resetreason" || text == "/why" || text == "/stats") {
                reply = getDetailedResetReport();
                bot.sendMessage(SECRET_TG_CHAT_ID, reply, "");
            }
            else if (text == "/faults") {
                reply = getFaultsReport();
                bot.sendMessage(SECRET_TG_CHAT_ID, reply, "");
            }
            else if (text == "/status") {
                bot.sendMessage(SECRET_TG_CHAT_ID, "🌡️ الحاضنة تعمل بشكل طبيعي.", "");
            }
            
            // ============================================================
            // ⭐ الأوامر الجديدة (بشكل محسّن)
            // ============================================================
            
            // ---- 1. عرض درجات الحرارة والرطوبة ----
            else if (text == "/temps") {
                String msg = "🌡️ **درجات الحرارة والرطوبة:**\n";
                msg += "========================\n";
                msg += "T1: " + String(dsSensors[0].temperature, 2) + " °C\n";
                msg += "T2: " + String(dsSensors[1].temperature, 2) + " °C\n";
                msg += "المتوسط: " + String(currentTemperature, 1) + " °C\n";
                msg += "الرطوبة: " + String(avgHumidity, 1) + " %\n";
                msg += "الحالة: " + String(hasValidTemperature ? "✅ صالحة" : "❌ غير صالحة");
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 2. عرض حالة المشغلات ----
            else if (text == "/actuators") {
                String msg = "⚡ **حالة المشغلات:**\n";
                msg += "=====================\n";
                msg += "🔥 السخان: " + String(heaterState ? "ON" : "OFF") + "\n";
                msg += "🌀 المروحة: " + String(actuators[0].state ? "ON" : "OFF") + "\n";
                msg += "🔄 المقلب: " + String(turnerState ? "ON" : "OFF") + "\n";
                msg += "💧 المرطب: " + String(actuators[3].state ? "ON" : "OFF") + "\n";
                msg += "🛡️ الوضع الآمن: " + String(safeMode ? "⚠️ مفعل" : "✅ غير مفعل") + "\n";
                msg += "🚨 الطوارئ: " + String(emergencyMode ? "⚠️ مفعل" : "✅ غير مفعل");
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 3. عرض معلومات الدورة ----
            else if (text == "/cycle") {
                unsigned long remaining = (TARGET_SEC > elapsedSec) ? (TARGET_SEC - elapsedSec) : 0;
                unsigned long hours = remaining / 3600;
                unsigned long mins = (remaining % 3600) / 60;
                unsigned long secs = remaining % 60;
                
                unsigned long totalSec = millis() / 1000;
                unsigned long totalHours = totalSec / 3600;
                unsigned long totalMins = (totalSec % 3600) / 60;
                
                String msg = "⏱️ **معلومات الدورة:**\n";
                msg += "=====================\n";
                msg += "رقم الدورة: " + String(cycleCount) + "\n";
                msg += "الوقت المتبقي: " + String(hours) + "س " + String(mins) + "د " + String(secs) + "ث\n";
                msg += "وقت التشغيل الكلي: " + String(totalHours) + "س " + String(totalMins) + "د\n";
                msg += "التقدم: " + String((elapsedSec * 100) / TARGET_SEC) + "%";
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 4. عرض تقرير صحي مختصر ----
            else if (text == "/health") {
                float score = getHealthScore();
                const char* cls = getHealthClass();
                String msg = "🩺 **تقرير الصحة المختصر:**\n";
                msg += "========================\n";
                msg += "الصحة: " + String(score, 1) + "/100\n";
                msg += "التصنيف: " + String(cls) + "\n";
                msg += "تجاوز الحرارة (P95): " + String(getP95Overshoot(), 2) + " °C\n";
                msg += "دورة السخان: " + String(getDutyCycle(), 1) + "%\n";
                msg += "فرق الحساسات (متوسط): " + String(getAvgDelta(), 2) + " °C\n";
                msg += "قراءات غير صالحة: " + String(getInvalidReads()) + "\n";
                msg += "انفصال الحساسات: " + String(sensorDisconnectCount) + "\n";
                msg += "التدهور: " + String(getDegradationStatus());
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 5. عرض معلومات النظام ----
            else if (text == "/sysinfo") {
                unsigned long uptimeSec = millis() / 1000;
                unsigned long days = uptimeSec / 86400;
                unsigned long hours = (uptimeSec % 86400) / 3600;
                unsigned long mins = (uptimeSec % 3600) / 60;
                
                String msg = "🖥️ **معلومات النظام:**\n";
                msg += "=====================\n";
                msg += "وقت التشغيل: " + String(days) + "يوم " + String(hours) + "س " + String(mins) + "د\n";
                msg += "الذاكرة الحرة: " + String((unsigned)ESP.getFreeHeap() / 1024) + " KB\n";
                msg += "أدنى ذاكرة: " + String((unsigned)ESP.getMinFreeHeap() / 1024) + " KB\n";
                msg += "رقم الإقلاع: " + String(bootCount) + "\n";
                msg += "آخر سبب إقلاع: " + String(last_reset_reason) + "\n";
                msg += "WiFi: " + String(wifiConnected ? "✅ متصل" : "❌ غير متصل") + "\n";
                msg += "الموديل: " + String(ESP.getChipModel()) + " | التردد: " + String(ESP.getCpuFreqMHz()) + " MHz";
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 6. تقرير تشخيصي شامل ----
            else if (text == "/diagnose") {
                unsigned long uptimeSec = millis() / 1000UL;
                unsigned long days = uptimeSec / 86400;
                unsigned long hours = (uptimeSec % 86400) / 3600;
                unsigned long mins = (uptimeSec % 3600) / 60;
                
                String report = "🔍 **تقرير تشخيصي شامل**\n";
                report += "==========================\n\n";
                report += "🕒 وقت التشغيل: " + String(days) + "يوم " + String(hours) + "س " + String(mins) + "د\n";
                report += "🩺 الصحة: " + String(getHealthScore(), 1) + "/100\n";
                report += "🌡️ الحرارة: " + String(currentTemperature, 1) + "°C\n";
                report += "💧 الرطوبة: " + String(avgHumidity, 1) + "%\n";
                report += "🔥 السخان: " + String(heaterState ? "ON" : "OFF") + "\n";
                report += "🔄 المقلب: " + String(turnerState ? "ON" : "OFF") + "\n";
                report += "🛡️ Safe Mode: " + String(safeMode ? "⚠️ مفعل" : "✅ غير مفعل") + "\n";
                report += "🚨 Emergency: " + String(emergencyMode ? "⚠️ مفعل" : "✅ غير مفعل") + "\n\n";
                report += "🔌 **إحصائيات إعادة التشغيل:**\n";
                report += "• إجمالي: " + String(resetStats.totalCount) + "\n";
                report += "• اليوم (آخر 24 ساعة): " + String(resetStats.dailyCount) + "\n";
                report += "• WDT: " + String(resetStats.wdtCount) + "\n";
                report += "• Panic: " + String(resetStats.panicCount) + "\n";
                report += "• Brownout: " + String(resetStats.brownoutCount);
                
                bot.sendMessage(SECRET_TG_CHAT_ID, report, "");
            }
            else if (text == "/logtoday") {
                // إرسال سجلات اليوم فقط
                String response = "📋 **سجلات اليوم**\n";
                int count = 0;
                for (uint32_t i = 0; i < logCount; i++) {
                    uint32_t idx = (logCount - 1 - i) % MAX_LOGS;
                    DetailedLog& l = logBuffer[idx];
                    if (String(l.timestamp).substring(0, 10) == getTimeString().substring(0, 10)) {
                        response += String(l.timestamp) + " | " + l.reason;
                        if (l.type == LOG_TYPE_FAULT) response += " | val=" + String(l.value);
                        response += "\n";
                        count++;
                        if (count >= 20) break; // حد أقصى 20 رسالة
                    }
                }
                if (count == 0) response += "✅ لا توجد أحداث اليوم.";
                bot.sendMessage(SECRET_TG_CHAT_ID, response, "");
            }
            else if (text == "/logall") {
                // إرسال آخر 10 سجلات (أو كلها حسب الحجم)
                String response = "📋 **آخر 10 أحداث**\n";
                int count = 0;
                for (uint32_t i = 0; i < logCount && count < 10; i++) {
                    uint32_t idx = (logCount - 1 - i) % MAX_LOGS;
                    DetailedLog& l = logBuffer[idx];
                    response += String(l.timestamp) + " | " + l.reason;
                    if (l.type == LOG_TYPE_FAULT) response += " | val=" + String(l.value);
                    response += "\n";
                    count++;
                }
                if (count == 0) response += "✅ لا توجد سجلات.";
                bot.sendMessage(SECRET_TG_CHAT_ID, response, "");
            }
            
            // ---- 7. أمر تشغيل/إيقاف التقارير التلقائية (جديد) ----
            else if (text == "/monitor") {
                telegram_set_monitoring(!monitorEnabled);
                String msg = "📊 **التقارير التلقائية:** ";
                msg += monitorEnabled ? "✅ **مفعلة** (كل 5 دقائق)" : "⏹️ **معطلة**";
                bot.sendMessage(SECRET_TG_CHAT_ID, msg, "");
            }
            
            // ---- 8. قائمة المساعدة المحدثة ----
            else if (text == "/help") {
                String help = "📋 **الأوامر المتاحة:**\n";
                help += "========================\n";
                help += "/temps - درجات الحرارة والرطوبة\n";
                help += "/actuators - حالة المشغلات\n";
                help += "/cycle - معلومات الدورة\n";
                help += "/health - تقرير صحي مختصر\n";
                help += "/sysinfo - معلومات النظام\n";
                help += "/diagnose - تقرير تشخيصي شامل\n";
                help += "/resetreason - أسباب إعادة التشغيل\n";
                help += "/faults - آخر الأعطال المسجلة\n";
                help += "/monitor - تشغيل/إيقاف التقارير التلقائية\n";
                help += "/status - الحالة العامة\n";
                help += "/help - هذه القائمة";
                bot.sendMessage(SECRET_TG_CHAT_ID, help, "");
            }
        }
        esp_task_wdt_reset();
        if (processed >= MAX_MESSAGES_PER_CALL) break;
        if ((millis() - t0) >= MAX_DRAIN_MS) break;
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        esp_task_wdt_reset();
    }
}