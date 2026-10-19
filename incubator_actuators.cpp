#include "incubator_internal.h"

void setActuator(int index, bool state) {
    // C8: مصدر واحد للحقيقة — مزامنة heaterState/turnerState مع actuators[]
    if (index < 0 || index >= ACTUATOR_COUNT) return;
    if (actuators[index].state == state) return;
    actuators[index].state = state;
    digitalWrite(actuators[index].pin, state ? HIGH : LOW);
    if (index == 2) heaterState = state;
    if (index == 1) turnerState = state;
    Serial.printf("⚡ %s -> %s\n", actuators[index].name, state ? "ON" : "OFF");
    diag_actuator_event(index);
}
// ---- إطفاء جميع السخانات ----
void forceAllHeatersOff() {
    for (int i = 0; i < ACTUATOR_COUNT; i++) {
        if (actuators[i].isHeater) {
            setActuator(i, false);
        }
    }
    heaterState = false;
}

// ---- تفعيل وضع الأمان (Safe Mode) ----
void enterSafeMode(const char* reason) {
    if (!safeMode) {
        safeMode = true;
        forceAllHeatersOff();
        setActuator(0, true);  // تشغيل المروحة
        setActuator(1, false); // إيقاف المقلب
        
        Serial.println("⚠️⚠️⚠️ SAFE MODE ACTIVATED ⚠️⚠️⚠️");
        Serial.print("Reason: ");
        Serial.println(reason);
        pref.putBool("safe_mode", true);
        {
            char logBuf[96];
            snprintf(logBuf, sizeof(logBuf), "Safe Mode: %s", reason);
            logEvent(logBuf);
        }
        addFault(FAULT_SAFE_MODE_ENTER, FAULT_EMERGENCY, 0);

    }
}

// ---- الخروج من وضع الأمان ----
void exitSafeMode() {
    
    if (safeMode) {
        safeMode = false;
        pref.putBool("safe_mode", false);
        Serial.println("✅ Safe mode cleared");
        logEvent("Safe Mode cleared");
    }
}

// ---- تفعيل وضع الطوارئ (Emergency Mode) ----
void enterEmergencyMode() {
    if (!emergencyMode) {
        emergencyMode = true;
        emergencyStartTime = millis();
        emergencyStableSince = 0;  // restart stability window on every entry
        forceAllHeatersOff();
        setActuator(0, true);  // تشغيل المروحة
        setActuator(1, false); // إيقاف المقلب
        
        Serial.println("🚨🚨🚨 EMERGENCY MODE ACTIVATED! 🚨🚨🚨");
        Serial.print("Temperature: ");
        Serial.print(rawTemperature);
        Serial.println("°C >= 39.8°C");
        
        pref.putBool("emergency", true);
        pref.putULong("emergency_time", millis());
        {
            char logBuf[64];
            snprintf(logBuf, sizeof(logBuf), "Emergency Mode: Temp %.1f°C", rawTemperature);
            logEvent(logBuf);
        }
        addFault(FAULT_EMERGENCY_TEMP, FAULT_EMERGENCY, rawTemperature);
    }
}

// ---- الخروج من وضع الطوارئ ----
void exitEmergencyMode() {
    if (emergencyMode) {
        emergencyMode = false;
        pref.putBool("emergency", false);
        Serial.println("✅ Emergency mode cleared");
        logEvent("Emergency Mode cleared");
    }
}

// ---- تسجيل الأحداث ----
void logEvent(const char* message) {
    unsigned long now = millis();
    unsigned long totalSec = now / 1000;
    unsigned long days = totalSec / 86400;
    unsigned long hours = (totalSec % 86400) / 3600;
    unsigned long mins = (totalSec % 3600) / 60;
    unsigned long secs = totalSec % 60;
    
    Serial.print("[");
    Serial.print(days);
    Serial.print("d ");
    Serial.print(hours);
    Serial.print("h ");
    Serial.print(mins);
    Serial.print("m ");
    Serial.print(secs);
    Serial.print("s] ");
    Serial.println(message);
}

// ---- فحص معقولية درجة الحرارة ----

void controlEvaporator() {
    // ---- 1. السلامة أولاً ----
    if (emergencyMode || safeMode) {
        if (evaporator.state) {
            evaporator.state = false;
            setActuator(3, false);
        }
        return;
    }

    // ---- 2. تحقق من صحة حساس الرطوبة ----
    bool hasValidHumidity = false;
    float currentHumidity = 0;

    for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
        // الحساس يعتبر صالحاً فقط إذا كان غير متجاوز (bypassed) و valid
        if (humiditySensors[i].valid && !humiditySensors[i].bypassed) {
            hasValidHumidity = true;
            currentHumidity = humiditySensors[i].humidity;
            break;
        }
    }

    // ---- 3. تحكم طبيعي (باستخدام الرطوبة) ----
    if (hasValidHumidity) {
        evaporator.useSafeTimedMode = false;
        // شغّل عند 45%، أوقف عند 55%
        if (currentHumidity < 45.0 && !evaporator.state) {
            evaporator.state = true;
            setActuator(3, true);
            Serial.println("Evaporator ON (Normal)");
        } else if (currentHumidity > 55.0 && evaporator.state) {
            evaporator.state = false;
            setActuator(3, false);
            Serial.println("Evaporator OFF (Normal)");
        }
        return;
    }

    // ---- 4. وضع آمن (دورة زمنية) عند فقدان الرطوبة ----
    // هنا نستخدم المنطق: إذا لم توجد قراءة صالحة (بسبب 3 فشلات متتالية)،
    // نعتمد على "آخر حالة معروفة" أو دورة زمنية آمنة بدلاً من التوقف المفاجئ.
    if (!hasValidHumidity) {
        evaporator.useSafeTimedMode = true;
        unsigned long now = millis();

        if (!evaporator.state) {
            // إذا كان المرطب مطفأ ومر وقت كافٍ → شغله
            if (now - evaporator.lastToggleTime >= evaporator.offDuration) {
                evaporator.state = true;
                setActuator(3, true);
                evaporator.lastToggleTime = now;
                Serial.println("Evaporator ON (Safe timed)");
            }
        } else {
            // إذا كان المرطب شغال ومر وقت كافٍ → أوقفه
            if (now - evaporator.lastToggleTime >= evaporator.onDuration) {
                evaporator.state = false;
                setActuator(3, false);
                evaporator.lastToggleTime = now;
                Serial.println("Evaporator OFF (Safe timed)");
            }
        }
    }
}// ========================================================================
// ========================================================================


void controlHeater() {
    if (safeMode || emergencyMode) {
        forceAllHeatersOff();
        return;
    }

    if (!hasValidTemperature) {
        forceAllHeatersOff();
        return;
    }

    // استخدام الإعدادات الديناميكية
    if (rawTemperature >= config.tempEmergency) {
        enterEmergencyMode();
        return;
    }

    if (rawTemperature >= config.tempCritical) {
        forceAllHeatersOff();
        return;
    }

    // Hysteresis باستخدام config
    if (!heaterState && currentTemperature <= config.tempLow) {
        setActuator(2, true);
        heaterOnStartTime = millis();
        heaterRuntimeAlert = false;
        logEvent("Heater ON");
    }
    else if (heaterState && currentTemperature >= config.tempHigh) {
        setActuator(2, false);
        heaterRuntimeAlert = false;
        logEvent("Heater OFF");
    }
}
// ========================================================================
// 11. دالة مراقبة وقت تشغيل السخان
// ========================================================================

void checkHeaterRuntime() {
    if (!heaterState) {
        heaterRuntimeAlert = false;
        return;
    }

    if (millis() - heaterOnStartTime >= config.heaterMaxRuntime) {
        if (!heaterRuntimeAlert) {
            heaterRuntimeAlert = true;
            Serial.println("⚠️ Warning: Heater running too long! Check door/insulation.");
            logEvent("Heater runtime warning");
            // يمكن إرسال تنبيه تيليجرام هنا لاحقاً
        }
    }
}

// ========================================================================
// Automatic Safety Mode & Emergency Mode Exit
// ========================================================================
void checkAutomaticSafetyExit() {
    unsigned long now = millis();

    // 1. Emergency Mode Automatic Exit when temperature stabilizes below TEMP_HIGH (37.8°C) for 10s
    if (emergencyMode) {
        if (hasValidTemperature && rawTemperature <= config.tempHigh) {
            if (emergencyStableSince == 0) {
                emergencyStableSince = now;
            } else if (now - emergencyStableSince >= 10000UL) {
                exitEmergencyMode();
                emergencyStableSince = 0;
            }
        } else {
            emergencyStableSince = 0;
        }
    } else {
        emergencyStableSince = 0;
    }

    // 2. Safe Mode Automatic Exit when sensors and relay status return to normal for 10s
    if (safeMode) {
        bool sensorsOk = hasValidTemperature;
        bool relayOk = !relayStuckFault && (rawTemperature < config.tempHigh || !heaterState);

        if (sensorsOk && relayOk) {
            if (safeStableSince == 0) {
                safeStableSince = now;
            } else if (now - safeStableSince >= 10000UL) {
                exitSafeMode();
                relayStuckFault = false;
                relayStuckTimer = 0;
                safeStableSince = 0;
            }
        } else {
            safeStableSince = 0;
        }
    } else {
        safeStableSince = 0;
    }
}
// ========================================================================
// 12. دالة تحديث العداد التنازلي
// ========================================================================

