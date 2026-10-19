#include "telegram_internal.h"

void fillDetailedResetReport(char* out, size_t n) {
    if (!out || n == 0) return;
    
    const char* reasonStr = "غير معروف";
    switch (resetStats.lastReasonCode) {
        case 1: reasonStr = "**Watchdog (توقف النظام)**"; break;
        case 2: reasonStr = "**Interrupt WDT (تعليق المقاطعة)**"; break;
        case 3: reasonStr = "**Panic (انهيار برمجي)**"; break;
        case 4: reasonStr = "**Brownout (انقطاع كهرباء مفاجئ)**"; break;
        case 5: reasonStr = "**إغلاق عادي (فصل كهرباء يدوي)**"; break;
        case 7: reasonStr = "**إعادة تشغيل برمجي (Software Reset)**"; break;
        case 8: reasonStr = "**Watchdog مهام (Task WDT)**"; break;
        case 9: reasonStr = "**تقلبات في مصدر الطاقة**"; break;
        case 10: reasonStr = "**إعادة تشغيل عبر USB**"; break;
        case 11: reasonStr = "**خروج من وضع النوم العميق**"; break;
        case 12: reasonStr = "**زر إعادة التشغيل الخارجي (EXT)**"; break;
        case 13: reasonStr = "**تجميد المعالج (CPU Lockup)**"; break;
        case 14: reasonStr = "**إعادة تشغيل عبر SDIO**"; break;
        case 15: reasonStr = "**إعادة تشغيل عبر JTAG**"; break;
        case 16: reasonStr = "**إعادة تشغيل بسبب EFUSE**"; break;
        default: break;
    }
    
    unsigned long uptimeSec = millis() / 1000UL;
    unsigned long mins = uptimeSec / 60;
    unsigned long secs = uptimeSec % 60;
    
    snprintf(out, n,
        "🔌 **تقرير سبب إعادة التشغيل المفصل**\n"
        "=============================\n"
        "● السبب الحالي: %s\n"
        "● وقت التشغيل الحالي: %lu دقيقة و %lu ثانية\n"
        "● حدث هذا السبب اليوم (آخر 24 ساعة): **%lu مرة**\n\n"
        "📊 **إحصائيات جميع الأسباب:**\n"
        "  • Watchdog / Interrupt WDT: %lu مرة\n"
        "  • Panic (انهيار برمجي): %lu مرة\n"
        "  • Brownout (انقطاع كهرباء): %lu مرة\n"
        "  • إغلاق عادي (فصل كهرباء يدوي): %lu مرة\n"
        "  • غير معروف: %lu مرة\n\n"
        "**إجمالي إعادة التشغيل الكلي:** %lu مرة\n"
        "**إجمالي المرات في آخر 24 ساعة:** %lu مرة",
        reasonStr,
        mins, secs,
        (unsigned long)resetStats.dailyCount,
        (unsigned long)resetStats.wdtCount,
        (unsigned long)resetStats.panicCount,
        (unsigned long)resetStats.brownoutCount,
        (unsigned long)resetStats.poweronCount,
        (unsigned long)resetStats.otherCount,
        (unsigned long)resetStats.totalCount,
        (unsigned long)resetStats.dailyCount);
}

// ============================================================
// بناء تقرير الأعطال بتنسيق جميل (بديل fillFaultsReport)
// ============================================================
void fillFaultsReport(char* out, size_t n) {
    if (!out || n == 0) return;
    
    size_t used = 0;
    int w = snprintf(out, n,
        "⚠️ **آخر 10 أعطال مسجلة:**\n"
        "==========================\n");
    if (w > 0) used = (size_t)w;
    
    uint8_t start = (faultHead >= 10) ? (faultHead - 10) : 0;
    int printed = 0;
    
    for (int i = 0; i < 10 && used + 64 < n; i++) {
        uint8_t idx = (start + i) % FAULT_RING_SIZE;
        if (faultRing[idx].ts == 0 && faultRing[idx].code == 0) continue;
        
        // تحويل الكود إلى وصف عربي
        const char* codeDesc = "عطل غير معروف";
        switch (faultRing[idx].code) {
            case 0x01: codeDesc = "انفصال حساس DS18B20"; break;
            case 0x02: codeDesc = "فرق كبير بين الحساسين"; break;
            case 0x05: codeDesc = "تجاوز الحرارة (>0.4°C)"; break;
            case 0x08: codeDesc = "طوارئ حرارة (≥39.8°C)"; break;
            case 0x10: codeDesc = "دخول Safe Mode"; break;
            case 0x20: codeDesc = "فشل النموذج التنبئي"; break;
            case 0x21: codeDesc = "تدهور الأداء"; break;
            default: break;
        }
        
        unsigned long mins = faultRing[idx].ts / 60;
        unsigned long secs = faultRing[idx].ts % 60;
        
        w = snprintf(out + used, n - used,
                     "• **%s** | القيمة: %.1f | منذ %luد %luث\n",
                     codeDesc, faultRing[idx].value, mins, secs);
        if (w > 0) used += (size_t)w;
        printed++;
    }
    
    if (printed == 0 && used + 30 < n) {
        snprintf(out + used, n - used, "✅ **لا توجد أعطال مسجلة.**");
    } else if (used + 30 < n) {
        snprintf(out + used, n - used, "\n_الأحدث في الأسفل._");
    }
}
// ============================================================
// دالة تحديث سبب إعادة التشغيل الأخير (تُستدعى في setup)
// ============================================================
void update_last_reset_reason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* msg = "غير معروف";
    switch (reason) {
        case ESP_RST_POWERON:    msg = "إغلاق عادي (فصل كهرباء)"; break;
        case ESP_RST_WDT:        msg = "Watchdog (توقف النظام)"; break;
        case ESP_RST_BROWNOUT:   msg = "انقطاع كهرباء مفاجئ"; break;
        case ESP_RST_PANIC:      msg = "انهيار برمجي (Panic)"; break;
        case ESP_RST_INT_WDT:    msg = "Interrupt WDT"; break;
        case ESP_RST_SW:         msg = "إعادة تشغيل برمجي (Software Reset)"; break;
        case ESP_RST_TASK_WDT:   msg = "Watchdog مهام (Task WDT)"; break;
        case ESP_RST_PWR_GLITCH: msg = "تقلبات في مصدر الطاقة"; break;
        case ESP_RST_USB:        msg = "إعادة تشغيل عبر USB"; break;
        case ESP_RST_DEEPSLEEP:  msg = "خروج من وضع النوم العميق"; break;
        case ESP_RST_EXT:        msg = "زر إعادة التشغيل الخارجي"; break;
        case ESP_RST_CPU_LOCKUP: msg = "تجميد المعالج (CPU Lockup)"; break;
        case ESP_RST_SDIO:       msg = "إعادة تشغيل عبر SDIO"; break;
        case ESP_RST_JTAG:       msg = "إعادة تشغيل عبر JTAG"; break;
        case ESP_RST_EFUSE:      msg = "إعادة تشغيل بسبب EFUSE"; break;
        default:                 msg = "غير معروف"; break;
    }
    snprintf(last_reset_reason, sizeof(last_reset_reason), "%s", msg);
}
String getDetailedResetReport() {
    // غلاف توافق — يُفضّل fillDetailedResetReport في المسارات الجديدة
    static char buf[768];
    fillDetailedResetReport(buf, sizeof(buf));
    return String(buf);
}

// ============================================================

String getFaultsReport() {
    static char buf[512];
    fillFaultsReport(buf, sizeof(buf));
    return String(buf);
}

// ============================================================
// دالة استقبال الأوامر من تيليجرام (غير حاجبة)
// ============================================================
// ============================================================
// دالة استقبال الأوامر من تيليجرام (غير حاجبة)
