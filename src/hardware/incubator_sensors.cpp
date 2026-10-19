#include "incubator_internal.h"

bool isPlausibleTemperature(float temp) {
    return (temp > 0.0 && temp < 50.0);
}

// ---- فحص القفزات المفاجئة ----
bool isTemperatureJumpValid(float temp, float lastTemp) {
    const float MAX_JUMP = 5.0;
    if (lastTemp == 0) return true;
    return abs(temp - lastTemp) <= MAX_JUMP;
}
// ---- تنعيم قراءة الحرارة (EMA) ----
float applyTempSmoothing(float newRawTemp) {
    if (!smootherInitialized) {
        currentTemperature = newRawTemp;
        smootherInitialized = true;
    } else {
        currentTemperature = (TEMP_SMOOTHING_ALPHA * newRawTemp)
                            + ((1.0 - TEMP_SMOOTHING_ALPHA) * currentTemperature);
    }
    return currentTemperature;
}
// ========================================================================
// 6. دالة كشف عطل الريلاي
// ========================================================================

void checkRelayStuckFault() {
     // لا داعي لفحص عطل الريلاي أثناء Emergency/Safe Mode - السخان مقفول عمداً بقرار النظام
    if (emergencyMode || safeMode) {
        relayStuckTimer = 0;
        return;
    }
    
    // إذا كان السخان مطفأ برمجياً لكن الحرارة مرتفعة جداً
if (!heaterState && (rawTemperature >= (config.tempHigh + RELAY_STUCK_MARGIN))) {        if (relayStuckTimer == 0) {
            relayStuckTimer = millis();
        } else {
            // Already returned if emergency/safe; fixed 30s window for stuck-on
            unsigned long requiredDuration = 30000UL;
            if (millis() - relayStuckTimer >= requiredDuration) {
                relayStuckFault = true;
                enterSafeMode("RELAY STUCK-ON DETECTED! HARDWARE FAILURE!");
            }
        }
    } else {
        relayStuckTimer = 0;
    }
}

// ========================================================================
// 7. دالة تهيئة الحساسات
// ========================================================================

void initSensors() {
    // ---- تهيئة DS18B20 ----
    ds18b20.begin();
    int deviceCount = ds18b20.getDeviceCount();
    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" DS18B20 sensors.");
    
    for (int i = 0; i < MAX_DS18B20_SENSORS; i++) {
        dsSensors[i].temperature = 0;
        dsSensors[i].valid = false;
        dsSensors[i].lastValidTemp = 0;
        dsSensors[i].failCount = 0;
    }
    // ---- تهيئة حساسات الرطوبة ----
    for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
        humiditySensors[i].dht = new DHT(dhtPins[i], DHT_TYPE);
        humiditySensors[i].dht->begin();
        humiditySensors[i].humidity = 0;
        humiditySensors[i].temperature = 0;
        humiditySensors[i].valid = false;
        humiditySensors[i].failCount = 0;
        humiditySensors[i].bypassed = false;
        humiditySensors[i].bypassStartTime = 0;
    }
    // // ---- تهيئة DHT ----
    // for (int i = 0; i < DHT_SENSOR_COUNT; i++) {
    //     dhtSensors[i].dht = new DHT(dhtPins[i], DHT_TYPE);
    //     dhtSensors[i].dht->begin();
    //     dhtSensors[i].temperature = 0;
    //     dhtSensors[i].humidity = 0;
    //     dhtSensors[i].valid = false;
    // }
}

// ---- قراءة DHT مع مهلة زمنية (Timeout) ----
// ---- إعادة تهيئة DHT عند الفشل ----
void resetHumiditySensor(int index) {
    if (index >= 0 && index < MAX_HUMIDITY_SENSORS) {
        Serial.printf("Resetting Humidity[%d]...\n", index);
        // Re-begin only – avoid delete/new heap fragmentation
        if (humiditySensors[index].dht) {
            humiditySensors[index].dht->begin();
        }
        humiditySensors[index].valid = false;
        humiditySensors[index].failCount = 0;
        humiditySensors[index].bypassed = false;
        humiditySensors[index].bypassStartTime = 0;
        {
            unsigned long t0 = millis();
            while ((millis() - t0) < 50UL) { esp_task_wdt_reset(); delay(1); }
        }
        { char logBuf[48]; snprintf(logBuf, sizeof(logBuf), "Humidity[%d] reset", index); logEvent(logBuf); }
    }
}
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


void readSensors() {
    // =====================================================================
    // 1. قراءة حساسات DS18B20 (مع مهلة 750 مللي)
    // =====================================================================
    ds18b20.setWaitForConversion(false);
    ds18b20.requestTemperatures();

    unsigned long startDS = millis();
    const unsigned long DS_TIMEOUT = 750;  // أقصى وقت لتحويل 12-bit

    // انتظار اكتمال التحويل مع مهلة
    while (!ds18b20.isConversionComplete()) {
        if (millis() - startDS >= DS_TIMEOUT) {
            esp_task_wdt_reset();  // ← أضف هذا السطر
            Serial.println("⚠️ DS18B20 conversion timeout!");
            break;
        }
        delay(1);
        esp_task_wdt_reset();
    }

    float tempSum = 0;
    int validCount = 0;

    // قراءة جميع حساسات DS18B20
    for (int i = 0; i < MAX_DS18B20_SENSORS; i++) {
        float temp = ds18b20.getTempCByIndex(i);
        dsSensors[i].temperature = temp;

        bool isConnected = (temp != DEVICE_DISCONNECTED_C);
        bool isPlausible = isPlausibleTemperature(temp);
        bool jumpValid = isTemperatureJumpValid(temp, dsSensors[i].lastValidTemp);
        bool isTimeout = (millis() - startDS >= DS_TIMEOUT);

        if (isConnected && isPlausible && jumpValid && !isTimeout) {
            dsSensors[i].valid = true;
            dsSensors[i].lastValidTemp = temp;
            dsSensors[i].failCount = 0;
            tempSum += temp;
            validCount++;
        } else {
            dsSensors[i].valid = false;
            dsSensors[i].failCount++;
            if (!isConnected) {
                Serial.printf("⚠️ DS18B20[%d] disconnected\n", i);
            } else if (!isPlausible) {
                Serial.printf("⚠️ DS18B20[%d] implausible: %.1f°C\n", i, temp);
            } else if (!jumpValid) {
                Serial.printf("⚠️ DS18B20[%d] jump: %.1f -> %.1f\n", i, dsSensors[i].lastValidTemp, temp);
            } else if (isTimeout) {
                Serial.printf("⚠️ DS18B20[%d] conversion timeout\n", i);
            }
        }
    }

    // حساب متوسط الحرارة (المستخدم في التحكم)
    if (validCount > 0) {
        rawTemperature = tempSum / validCount;
        applyTempSmoothing(rawTemperature);
        hasValidTemperature = true;
        avgValid = true;
    } else {
        rawTemperature = 0;
        hasValidTemperature = false;
        avgValid = false;
        // لا نمسح smootherInitialized للحفاظ على القيمة الناعمة
    }

    // =====================================================================
    // 2. قراءة حساسات DHT (طريقة آمنة - مرة واحدة فقط)
    // =====================================================================
        // =====================================================================
    // 2. قراءة حساسات DHT (مع إعادة محاولة تلقائية دورية)
    // =====================================================================
// 2. قراءة حساسات DHT (مع منطق Debounce - مثل DS18B20 تماماً)
    // =====================================================================
    // =====================================================================
    // 2. قراءة حساسات DHT (مع منطق Debounce - مثل DS18B20 تماماً)
    // =====================================================================
    for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
        // ---- التحقق من التجاوز وإعادة المحاولة الدورية ----
        if (humiditySensors[i].bypassed) {
            if (millis() - humiditySensors[i].bypassStartTime >= DHT_RETRY_INTERVAL) {
                humiditySensors[i].bypassed = false;
                humiditySensors[i].failCount = 0; // إعادة تعيين العداد
                Serial.printf("🔄 Humidity[%d] retry attempt\n", i);
            } else {
                humiditySensors[i].valid = false;
                humiditySensors[i].humidity = 0;
                humiditySensors[i].temperature = 0;
                continue;
            }
        }

        // ---- قراءة الحساس بطريقة آمنة (تمنع تعليق النظام) ----
        unsigned long t0 = millis();
        
        esp_task_wdt_reset(); // إطعام المؤقت قبل القراءة
        float h = humiditySensors[i].dht->readHumidity();
        float t = humiditySensors[i].dht->readTemperature();
        esp_task_wdt_reset(); // إطعام المؤقت بعد القراءة

        // **حماية قاتلة:** إذا استغرقت المكتبة أكثر من 50ms، فهي معلقة
        if (millis() - t0 > 100UL) {
            h = NAN;
            t = NAN;
        }

        // ---- نجاح القراءة ----
        if (!isnan(h) && !isnan(t) && h != 0.0 && t != 0.0) {
            humiditySensors[i].humidity = h;
            humiditySensors[i].temperature = t;
            humiditySensors[i].valid = true;
            
            // منطق الـ Debounce: زيادة عداد النجاح
            if (humiditySensors[i].failCount > 0) {
                humiditySensors[i].failCount--; // نقص عداد الفشل تدريجياً
            }
            
            if (humiditySensors[i].failCount == 0) {
                // إذا عاد العداد للصفر، الحساس سليم تماماً
                if (humiditySensors[i].bypassed) {
                    humiditySensors[i].bypassed = false;
                    Serial.printf("✅ Humidity[%d] reconnected!\n", i);
                    logEvent("Humidity reconnected");
                }
            }
        } 
        // ---- فشل القراءة ----
        else {
            humiditySensors[i].valid = false;
            humiditySensors[i].humidity = 0;
            humiditySensors[i].temperature = 0;
            humiditySensors[i].failCount++;

            // منطق الـ Debounce: إذا فشل 3 مرات متتالية -> يُعتبر مفقوداً مؤقتاً
            if (humiditySensors[i].failCount >= 3) {
                humiditySensors[i].bypassed = true;
                humiditySensors[i].bypassStartTime = millis();
                Serial.printf("⛔ Humidity[%d] bypassed for %d s\n", i, DHT_RETRY_INTERVAL / 1000);
                logEvent("Humidity bypassed");
            }
        }
    }
    // ---- إحصائيات الرطوبة للتشخيص (توسع مستقبلي) ----
    float humSum = 0; int humCount = 0;
    for (int i = 0; i < MAX_HUMIDITY_SENSORS; i++) {
        if (humiditySensors[i].valid && !humiditySensors[i].bypassed) {
            humSum += humiditySensors[i].humidity;
            humCount++;
        }
        if (!humiditySensors[i].valid) humidityFailCount++;
    }
    if (humCount > 0) avgHumidity = humSum / humCount;
    humiditySamples += humCount;
}
// ========================================================================
// 9. دالة تحديث شاشة LCD
// ========================================================================
