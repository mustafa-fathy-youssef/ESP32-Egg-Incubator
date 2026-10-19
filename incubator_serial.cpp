#include "incubator_internal.h"

void handleSerialCommands() {
    if (!Serial.available()) return;

    char c = Serial.read();

    if (c == '\n' || c == '\r') {
        // ---- أمر RESET ----
        if (strcmp(serialBuffer, "RESET") == 0 || strcmp(serialBuffer, "reset") == 0) {
            elapsedSec = 0;
            lastMillis = millis();
            lastTurnerTime = millis();
            pref.putULong("elapsed_sec", 0);

            if (safeMode) {
                exitSafeMode();
                Serial.println("✅ Safe Mode cleared by RESET");
            }
            if (emergencyMode) {
                exitEmergencyMode();
                Serial.println("✅ Emergency Mode cleared by RESET");
            }

            Serial.println("🔄 Reset Done!");
            logEvent("Manual reset via Serial");
        }
        // ---- أمر STATUS ----
        else if (strcmp(serialBuffer, "STATUS") == 0 || strcmp(serialBuffer, "status") == 0) {
            Serial.println("===== System Status =====");
            Serial.printf("Cycle: %lu / %lu seconds\n", elapsedSec, TARGET_SEC);
            Serial.printf("Temperature (raw): %.1f°C\n", rawTemperature);
            Serial.printf("Temperature (smoothed): %.1f°C\n", currentTemperature);
            Serial.printf("Heater: %s\n", heaterState ? "ON" : "OFF");
            Serial.printf("Turner: %s\n", turnerState ? "ON" : "OFF");
            Serial.printf("Safe Mode: %s\n", safeMode ? "ACTIVE" : "OFF");
            Serial.printf("Emergency: %s\n", emergencyMode ? "ACTIVE" : "OFF");
            Serial.printf("Cycle Count: %lu\n", cycleCount);
            Serial.printf("Sensor Valid: %s\n", hasValidTemperature ? "YES" : "NO");
            Serial.println("=========================");
        }
        // ---- أمر EXITSAFE ----
        else if (strcmp(serialBuffer, "EXITSAFE") == 0 || strcmp(serialBuffer, "exitsafe") == 0) {
            if (safeMode) {
                exitSafeMode();
                setActuator(0, true);
                Serial.println("✅ Safe Mode cleared manually");
            } else {
                Serial.println("ℹ️ Safe Mode is not active");
            }
        }
        // ---- أمر EXITEMG ----
        else if (strcmp(serialBuffer, "EXITEMG") == 0 || strcmp(serialBuffer, "exitemg") == 0) {
            if (emergencyMode) {
                exitEmergencyMode();
                Serial.println("✅ Emergency Mode cleared manually");
            } else {
                Serial.println("ℹ️ Emergency Mode is not active");
            }
        }
        // ---- أمر RESETDHT ----
        else if (strcmp(serialBuffer, "RESETDHT") == 0 || strcmp(serialBuffer, "resetdht") == 0) {
            for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
                resetHumiditySensor(i);
            }
            Serial.println("🔄 All humidity sensors have been reset!");
        }
        // ---- أمر REPORT ----
        else if (strcmp(serialBuffer, "REPORT") == 0 || strcmp(serialBuffer, "report") == 0) {
            printFullReport();
        }
        // ---- أمر HEALTH ----
        else if (strcmp(serialBuffer, "HEALTH") == 0 || strcmp(serialBuffer, "health") == 0) {
            printHealthSummary();
        }
        // ---- أمر DIAG ----
        else if (strcmp(serialBuffer, "DIAG") == 0 || strcmp(serialBuffer, "diag") == 0) {
            printDiagStatus();
        }
        else if (strcmp(serialBuffer, "FAULTS") == 0 || strcmp(serialBuffer, "faults") == 0) {
            printFaultRing();
        }
        // ---- أمر اختبار التيليجرام ----
                // ---- أوامر مراقبة التيليجرام التلقائية ----
        // ---- أوامر مراقبة التيليجرام التلقائية ----
        else if (strcmp(serialBuffer, "MONITORON") == 0) {
            telegram_set_monitoring(true);
            Serial.println("✅ Periodic monitoring ENABLED (every 5 min)"); // C5
        }
        else if (strcmp(serialBuffer, "MONITOROFF") == 0) {
            telegram_set_monitoring(false);
            Serial.println("⏹️ Periodic monitoring DISABLED");
        }else if (strcmp(serialBuffer, "TESTTG") == 0) {
            Serial.println("📲 Sending test Telegram...");
            // استخدام رقم عشوائي ككود عطل مختلف في كل مرة
            telegram_send_fault(random(1, 255), 1, 0.0f); 
        }
        // ---- أمر CLEARDIAG (بدء عملية المسح) ----
        else if (strcmp(serialBuffer, "CLEARDIAG") == 0 || strcmp(serialBuffer, "cleardiag") == 0) {
            if (waitingForConfirm) {
                Serial.println("⚠️ Confirmation already pending. Type 'CONFIRM' or wait.");
            } else {
                Serial.println("⚠️ WARNING: This will erase ALL diagnostics data (history, faults, stats).");
                Serial.println("⚠️ Type 'CONFIRM' within 5 seconds to proceed.");
                waitingForConfirm = true;
                confirmTimeout = millis() + 5000;
            }
        }
        // ---- أمر FORECAST (Phase 7) ----
        else if (strcmp(serialBuffer, "FORECAST") == 0 || strcmp(serialBuffer, "forecast") == 0) {
            cycle_archive_printForecast();
        }
        // ---- أمر HEAP (free SRAM diagnostic) ----
        else if (strcmp(serialBuffer, "HEAP") == 0 || strcmp(serialBuffer, "heap") == 0) {
            Serial.printf("🧠 Heap free: %u bytes | min ever: %u bytes | firmware %s\n",
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getMinFreeHeap(),
                          FIRMWARE_VERSION);
        }
        // ---- أمر VERSION ----
        else if (strcmp(serialBuffer, "VERSION") == 0 || strcmp(serialBuffer, "version") == 0) {
            Serial.printf("%s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
            Serial.printf("  Chip: %s  Rev: %d  CPU: %u MHz\n",
                          ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
        }        // ---- SET_TEMP ----
        else if (strncmp(serialBuffer, "SET_TEMP ", 9) == 0) {
            float val = atof(serialBuffer + 9);
            if (val >= 30.0f && val <= 42.0f) {
                config.targetTemp = val;
                config.tempLow  = val - 0.3f;
                config.tempHigh = val + 0.1f;
                saveConfig();
                Serial.printf("✅ Target Temp set to %.1f°C (Low=%.1f High=%.1f)\n",
                              config.targetTemp, config.tempLow, config.tempHigh);
            } else {
                Serial.println("❌ SET_TEMP range: 30.0 – 42.0");
            }
        }
        // ---- SET_CRITICAL ----
        else if (strncmp(serialBuffer, "SET_CRITICAL ", 13) == 0) {
            float val = atof(serialBuffer + 13);
            if (val >= 38.0f && val <= 45.0f) {
                config.tempCritical = val;
                saveConfig();
                Serial.printf("✅ Critical Temp set to %.1f°C\n", config.tempCritical);
            } else {
                Serial.println("❌ SET_CRITICAL range: 38.0 – 45.0");
            }
        }
        // ---- SET_EMERGENCY ----
        else if (strncmp(serialBuffer, "SET_EMERGENCY ", 14) == 0) {
            float val = atof(serialBuffer + 14);
            if (val >= 38.0f && val <= 45.0f && val > config.tempCritical) {
                config.tempEmergency = val;
                saveConfig();
                Serial.printf("✅ Emergency Temp set to %.1f°C\n", config.tempEmergency);
            } else {
                Serial.println("❌ SET_EMERGENCY must be > Critical and within 38–45");
            }
        }
        // ---- SET_TURNER_ON ----
        else if (strncmp(serialBuffer, "SET_TURNER_ON ", 14) == 0) {
            int sec = atoi(serialBuffer + 14);
            if (sec >= 5 && sec <= 60) {
                config.turnerOnTime = (unsigned long)sec * 1000UL;
                saveConfig();
                Serial.printf("✅ Turner ON time set to %d s\n", sec);
            } else {
                Serial.println("❌ SET_TURNER_ON range: 5 – 60 seconds");
            }
        }
        // ---- SET_TURNER_OFF ----
        else if (strncmp(serialBuffer, "SET_TURNER_OFF ", 15) == 0) {
            int sec = atoi(serialBuffer + 15);
            if (sec >= 30 && sec <= 600) {
                config.turnerOffTime = (unsigned long)sec * 1000UL;
                saveConfig();
                Serial.printf("✅ Turner OFF time set to %d s\n", sec);
            } else {
                Serial.println("❌ SET_TURNER_OFF range: 30 – 600 seconds");
            }
        }
        // ---- SHOW_CONFIG ----
        else if (strcmp(serialBuffer, "SHOW_CONFIG") == 0 || strcmp(serialBuffer, "show_config") == 0) {
            Serial.println("========== CURRENT CONFIG ==========");
            Serial.printf("  Target Temp     : %.2f °C\n", config.targetTemp);
            Serial.printf("  Temp Low        : %.2f °C\n", config.tempLow);
            Serial.printf("  Temp High       : %.2f °C\n", config.tempHigh);
            Serial.printf("  Temp Critical   : %.2f °C\n", config.tempCritical);
            Serial.printf("  Temp Emergency  : %.2f °C\n", config.tempEmergency);
            Serial.printf("  Turner ON       : %lu s\n", config.turnerOnTime / 1000UL);
            Serial.printf("  Turner OFF      : %lu s\n", config.turnerOffTime / 1000UL);
            Serial.printf("  Heater Max Run  : %lu s\n", config.heaterMaxRuntime / 1000UL);
            Serial.printf("  NVS Save Interv : %lu s\n", config.nvsSaveInterval);
            Serial.println("====================================");
        }
        // ---- أمر HELP ----
        else if (strcmp(serialBuffer, "HELP") == 0 || strcmp(serialBuffer, "help") == 0 ||
                 strcmp(serialBuffer, "?") == 0) {
            Serial.println("Available commands:");
            Serial.println("  STATUS    - Live system status");
            Serial.println("  REPORT    - Full reliability report");
            Serial.println("  HEALTH    - Short health line");
            Serial.println("  DIAG      - Internal diagnostics counters");
            Serial.println("  FAULTS    - Fault ring (last events)");
            Serial.println("  FORECAST  - Health degradation projection");
            Serial.println("  HEAP      - Free / min free heap");
            Serial.println("  VERSION   - Firmware and chip info");
            Serial.println("  RESET     - Reset timer; clear Safe + Emergency");
            Serial.println("  EXITSAFE  - Exit Safe Mode only");
            Serial.println("  EXITEMG   - Exit Emergency Mode only");
            Serial.println("  RESETDHT  - Re-begin humidity sensor(s)");
            Serial.println("  CLEARDIAG - Wipe diagnostics (then CONFIRM within 5s)");
            Serial.println("  HELP      - This list");
            Serial.println("  MONITORON - Send Telegram report every 5 min");
            Serial.println("  MONITOROFF - Stop periodic Telegram reports");
        }
        else if (strcmp(serialBuffer, "PUBLISH") == 0) {
            publishAllFeeds();
        }        // ---- أوامر غير معروفة ----
        else {
            Serial.println("❌ Unknown command. Type HELP for the full list.");
        }

        // مسح المخزن المؤقت بعد معالجة الأمر
        serialBuffer[0] = '\0';
        serialIdx = 0;
    } else {
        // تجميع الحروف مع حد أقصى
        if (serialIdx < sizeof(serialBuffer) - 1) {
            serialBuffer[serialIdx++] = c;
            serialBuffer[serialIdx] = '\0';
        }
    }
}

// ============================================================
// دالة اختبار الـ Core Dump (تُسبب انهياراً متعمداً)
// ============================================================
// void testCoreDump() {
//     Serial.println("🔥 Crashing immediately with null pointer dereference...");
//     // تأخير قصير جداً (مثل 100 مللي) أو بدون تأخير
//     delay(100); 
    
//     // طريقة مضمونة للانهيار الفوري (بدون تعطيل الـ Watchdog)
//     volatile int* p = nullptr; 
//     volatile int crash = *p; // محاولة قراءة من عنوان 0x00000000
    
//     Serial.println(crash); // لن تصل هنا أبداً
// }
// تعريف bootCount كمتغير عام (في أعلى Memory10_2.ino قبل setup)
uint8_t bootCount = 0;

// ============================================================
// Task WDT — safe for Arduino-ESP32 core 3.x
// Call BEFORE any delay / NTP / long work in setup()
// ============================================================
static void setupWatchdog() {
    esp_task_wdt_config_t cfg = {
        .timeout_ms = (uint32_t)WDT_TIMEOUT * 1000UL,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };

    // Core 3.x often already initialized TWDT → reconfigure instead of double init
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        // Not initialized yet
        err = esp_task_wdt_init(&cfg);
    } else if (err != ESP_OK) {
        esp_task_wdt_deinit();
        err = esp_task_wdt_init(&cfg);
    }

    esp_err_t addErr = esp_task_wdt_add(NULL);  // subscribe loopTask
    Serial.printf("WDT: config=%s add=%s timeout=%ds\n",
                  esp_err_to_name(err), esp_err_to_name(addErr), WDT_TIMEOUT);
}

