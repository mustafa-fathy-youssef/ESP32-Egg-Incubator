#include "incubator_internal.h"

void reinitLCD() {
    // تأكد من أن الـ I2C يستجيب قبل التهيئة
    Wire.beginTransmission(0x27);
    if (Wire.endTransmission() != 0) {
        Serial.println("❌ LCD still not responding, aborting reinit.");
        lcdInitialized = false;
        return;
    }
    esp_task_wdt_reset();
    lcd.init();
    esp_task_wdt_reset();

    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Reinit OK");
    // C7: لا delay خام — غذّ WDT أثناء الانتظار
    {
        unsigned long t0 = millis();
        while (millis() - t0 < 200UL) {
            esp_task_wdt_reset();
            delay(10);
            esp_task_wdt_reset();  // بعد كل delay طويل

        }
    }
    lcdInitialized = true;
    Serial.println("✅ LCD reinitialized successfully.");
}

void updateLCD() {
    bool dhtOk = false;
    float humidity = 0;
    float dhtTemp = 0;
    if (!lcdInitialized) return;

    for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
        if (humiditySensors[i].valid && !humiditySensors[i].bypassed) {
            dhtOk = true;
            humidity = humiditySensors[i].humidity;
            dhtTemp = humiditySensors[i].temperature;
            break;
        }
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    
    // ---- السطر الأول: درجة الحرارة والرطوبة ----
    if (avgValid) {
        lcd.print("Avg:");
        lcd.print(currentTemperature, 1);
        lcd.print("C ");
        if (dhtOk) {
            lcd.print("H:");
            lcd.print(humidity, 0);
            lcd.print("%");
        } else {
            lcd.print("H:--");
        }
    } else if (dsSensors[0].valid) {
        lcd.print("T1:");
        lcd.print(dsSensors[0].temperature, 1);
        lcd.print("C ");
        if (dhtOk) {
            lcd.print("H:");
            lcd.print(humidity, 0);
            lcd.print("%");
        } else {
            lcd.print("H:--");
        }
    } else if (dsSensors[1].valid) {
        lcd.print("T2:");
        lcd.print(dsSensors[1].temperature, 1);
        lcd.print("C ");
        if (dhtOk) {
            lcd.print("H:");
            lcd.print(humidity, 0);
            lcd.print("%");
        } else {
            lcd.print("H:--");
        }
    } else {
        lcd.print("Sensor Error!");
    }
    
    // ---- السطر الثاني: الوقت المتبقي + حالة الطوارئ ----
    unsigned long remainingSec = 0;
    if (TARGET_SEC > elapsedSec) {
        remainingSec = TARGET_SEC - elapsedSec;
    }
    
    unsigned long remMin = remainingSec / 60;
    unsigned long remSec = remainingSec % 60;
    
    lcd.setCursor(0, 1);
    lcd.print("R:");
    if (remMin < 10) lcd.print("0");
    lcd.print(remMin);
    lcd.print(":");
    if (remSec < 10) lcd.print("0");
    lcd.print(remSec);
    
    // عرض حالة الطوارئ أو الأمان
    lcd.setCursor(11, 1);
    if (emergencyMode) {
        lcd.print("EMG");
    } else if (safeMode) {
        lcd.print("SAF");
    } else {
        lcd.print("   ");
    }
}

// ========================================================================
// 10. دالة التحكم في السخان
// ========================================================================

