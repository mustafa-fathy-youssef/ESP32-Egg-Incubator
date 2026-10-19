#include "diagnostics_internal.h"

// diagnostics.cpp
// Phase 1 – Foundation: Welford, bandTime, counters, REPORT, NVS persistence
// Observational only – never modifies control logic.

#include "diagnostics.h"
#include <Preferences.h>
#include <esp_system.h>
#include "telegram_alerts.h"  
#include <math.h>
#include <string.h>
// #include "diag_io.h"
// ---- FIX: تمت إزالة تعريف struct Actuator المكرر هنا - أصبح
// يُستورد من diagnostics.h (المصدر الوحيد الآن) بدلاً من نسخة
// محلية منفصلة كانت قد تنحرف عن نسخة Memory5.ino بصمت.
extern float avgHumidity;
extern uint32_t humidityFailCount;
extern uint32_t humiditySamples;
// ---- إعلانات مسبقة للدوال ----
bool loadDiagnosticsBlob();
void saveDiagnosticsBlob();

// ============================================================
// تعريف الهياكل الأساسية (في النطاق العام)
// ============================================================

// ThermalStats: Welford online mean/variance
struct ThermalStats
{
    uint32_t n;
    float mean;
    float m2; // sum of squared differences from mean
    float minT;
    float maxT;
};


// ============================================================
// المرحلة النهائية: المصفوفات اليومية وحساب التدهور
// ============================================================
static float dayDeltaAvg[30] = {0};
static float dayDuty[30] = {0};
static float dayOverAvg[30] = {0};
static float dayMeanDev[30] = {0};
static uint8_t dayHealth[30] = {0};
static uint8_t dayIdx = 0;           // اليوم الحالي (0-29)
static uint8_t dayCountFilled = 0;   // grows to 30 then stays (sliding window size)
static unsigned long lastDayStart = 0; // وقت بداية اليوم الحالي (millis)

// ---- عدادات المشغلات (Actuator) ----
static uint32_t diagSwitchCount[4] = {0, 0, 0, 0};  // 0=Fan, 1=Turner, 2=Heater, 3=Evaporator
static uint32_t diagOnMs[4] = {0, 0, 0, 0};
static unsigned long lastOnStart[4] = {0, 0, 0, 0};

// ---- إحصائيات الرطوبة (للتوسع المستقبلي) ----
// تم نقل التعريفات إلى Memory4.ino واستخدامها عبر extern

// ============================================================
// هيكل التخزين المزدوج مع CRC (Dual‑blob)
// ============================================================
#pragma pack(push, 1)
struct DiagBlob {
    uint32_t magic;          // 0xABADBABE
    uint32_t version;        // 1
    uint32_t timestamp;      // وقت الحفظ (millis)
    uint16_t crc;            // CRC16 للتحقق

    // ---- الإحصائيات الأساسية ----
    ThermalStats thermal;
    float bandTime[6];
    uint32_t totalSamples;
    uint32_t heaterCycles;
    uint32_t diagUptimeSec;

    // ---- عدادات إعادة التشغيل ----
    uint32_t resetUnexpected;
    uint32_t resetWdt;
    uint32_t resetBrownout;

    // ---- انقطاع الحساسات ----
    uint32_t sensorDisconnectCount;

    // ---- التجاوز (Overshoot) ----
    uint32_t overCount;
    float overSum;
    float overMax;
    uint32_t overHist[20];

    // ---- فرق الحساسات (Delta) ----
    uint32_t deltaN;
    float deltaSum;
    float deltaMax;

    // ---- أداء السخان ----
    uint32_t heaterOnTotalMs;
    uint32_t onTimeSumMs;
    uint32_t onTimeCount;
    uint32_t invalidReads;
    uint32_t tempHist[150];

    // ---- المصفوفات اليومية (30 يوم) ----
    float dayDeltaAvg[30];
    float dayDuty[30];
    float dayOverAvg[30];
    float dayMeanDev[30];
    uint8_t dayHealth[30];
    uint8_t dayIdx;
    uint32_t lastDayStart;

    // ---- آخر 8 أعطال للحفظ ----
    FaultRec savedFaults[8];
};
#pragma pack(pop)

static DiagBlob blob;
static bool blobDirty = false;
static uint8_t activeBlobIdx = 0; // 0 أو 1
static const char* BLOB_KEYS[2] = {"diag_blk_a", "diag_blk_b"};
// ============================================================
static unsigned long lastSaveTime = 0;
// إعلان الدوال الخارجية (معرفة في Memory3.ino)
// ============================================================
void logEvent(const char* message);
// ============================================================
// تعريف الهيكل المستخدم من Memory3.ino (نسخة مطابقة)
// ============================================================
// DSTemperatureSensor defined once in diagnostics.h
extern DSTemperatureSensor dsSensors[2];
// ============================================================
// 1. Data structures & state (static, internal to this module)
// ============================================================

// تعريف المتغير العام thermal (يستخدم في كل مكان)
static ThermalStats thermal = {0, 0.0f, 0.0f, 999.0f, -999.0f};
// ============================================================
// Phase 2: Overshoot Tracker & Sensor Delta
// ============================================================

// Heater Overshoot State Machine
struct HeaterOvershoot
{
    bool armed;
    unsigned long offMs;
    float offTemp;
    float peak;
    uint32_t samples;
};
static HeaterOvershoot overshoot = {false, 0, 0.0f, 0.0f, 0};

// Overshoot histogram (bins of 0.05°C, from 0.0 to 1.0°C)
static uint32_t overHist[20] = {0};

// Sensor Delta stats (difference between DS18B20 sensors)
struct SensorDelta
{
    uint32_t n;
    float sum;
    float max;
};
static SensorDelta sensorDelta = {0, 0.0f, 0.0f};

// Running stats for overshoot
static uint32_t overCount = 0;
static float overSum = 0.0f;
static float overMax = 0.0f;
// Temperature bands (seconds spent in each):
// 0: <36.0, 1: 36.0-37.0, 2: 37.0-37.5, 3: 37.5-38.0, 4: 38.0-38.5, 5: >38.5
static float bandTime[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// Counters
static uint32_t totalSamples = 0;
static uint32_t heaterCycles = 0; // counts OFF transitions only
static uint32_t diagUptimeSec = 0;

// Reset reason counters (accumulated since last clear)
static uint32_t resetUnexpected = 0;
static uint32_t resetWdt = 0;
static uint32_t resetBrownout = 0;

// Internal flags
static bool heaterWasOn = false; // for edge detection
// تم استبدال dirty بـ blobDirty (يُستخدم في saveDiagnosticsBlob)
static const unsigned long SAVE_INTERVAL_MS = 10000UL; // 10 ثواني 

// ============================================================
// Phase 3: Sensor Disconnect Counter
// ============================================================
uint32_t sensorDisconnectCount = 0;
static bool lastSensorDisconnectState[2] = {false, false};

// ============================================================
// Phase 3: Fault Ring Buffer (32 events)
// ============================================================
// Phase 3.5: متغيرات إضافية (Duty Cycle, P95, Invalid Reads)
// ============================================================

// رسم بياني للحرارة (150 خانة من 30.0 إلى 45.0 درجة، بدقة 0.1)
static uint32_t tempHist[150] = {0};

// عدادات السخان
static uint32_t heaterOnTotalMs = 0;       // إجمالي وقت تشغيل السخان (مللي ثانية)
static uint32_t onTimeSumMs = 0;           // مجموع أوقات تشغيل السخان في كل دورة (مللي ثانية)
static uint32_t onTimeCount = 0;           // عدد دورات تشغيل السخان
static unsigned long currentOnStartMs = 0; // وقت بدء التشغيل الحالي

// عداد القراءات غير الصالحة (لحساسات DS18B20 فقط)
static uint32_t invalidReads = 0;
// ============================================================
// FIX (Phase 7 precision): متتبّع "الدورة الحالية فقط" عبر لقطة
// (baseline) من العدادات التراكمية الموجودة أصلاً، ثم الفرق.
// لا يلمس منطق diag_sample() الحالي إطلاقاً - فقط يقرأ عدادات
// تراكمية موجودة أصلاً (heaterOnTotalMs, diagUptimeSec, overCount,
// overSum) ويحسب الفرق منذ آخر بداية دورة.
// ============================================================
static uint32_t cycleBaselineHeaterOnMs   = 0;
static unsigned long cycleBaselineUptimeSec = 0;
static uint32_t cycleBaselineOverCount    = 0;
static float    cycleBaselineOverSum      = 0.0f;
// ============================================================
#define FAULT_RING_SIZE 32
// #define FAULT_CRITICAL 2
// #define FAULT_EMERGENCY 3

// تعريف FaultRec مستخدم هنا (من النطاق العام أعلاه)
FaultRec faultRing[FAULT_RING_SIZE];
uint8_t faultHead = 0;
static uint32_t faultCount = 0;

// Fault codes
#define FAULT_OVERSHOOT_HIGH 0x05
#define FAULT_EMERGENCY_TEMP 0x08
#define FAULT_SENSOR_DISCONNECT 0x01
#define FAULT_SAFE_MODE_ENTER 0x10
#define FAULT_SENSOR_DELTA_HIGH 0x02
#define FAULT_WDT_RESET 0x0B
#define FAULT_BROWNOUT_RESET 0x0C

// Preferences object (kept open)
static Preferences prefDiag;
static Preferences prefLog;

// ============================================================
// 2. Helper: get band index from temperature
// ============================================================
static int getBandIndex(float temp)
{
    if (temp < 36.0f)
        return 0;
    if (temp < 37.0f)
        return 1;
    if (temp < 37.5f)
        return 2;
    if (temp < 38.0f)
        return 3;
    if (temp < 38.5f)
        return 4;
    return 5;
}


// ============================================================
// CRC16 (CCITT) بسيطة وسريعة
// ============================================================
static uint16_t calculateCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

// ============================================================
// 3. diag_init – call once in setup()
// ============================================================
// تم إزالة الكود القديم المعلق (يحتفظ بالتعليقات)
// ============================================================
// تتبع المشغلات (تُستدعى من setActuator)
// ============================================================
void diag_actuator_event(int idx) {
    if (idx < 0 || idx >= 4) return; // 0=Fan, 1=Turner, 2=Heater, 3=Evaporator
    
    bool newState = actuators[idx].state;
    unsigned long now = millis();
    
    if (newState) {
        // بداية تشغيل
        lastOnStart[idx] = now;
    } else {
        // نهاية تشغيل → احسب المدة
        if (lastOnStart[idx] != 0) {
            diagOnMs[idx] += (now - lastOnStart[idx]);
            lastOnStart[idx] = 0;
        }
        diagSwitchCount[idx]++;
        blobDirty = true;
    }
}
void diag_init() {
    prefDiag.begin("inc_diag", false);

    // ---- محاولة استعادة البيانات من النسخة المزدوجة ----
    if (loadDiagnosticsBlob()) {
        // استعادة كل المتغيرات من الـ blob
        thermal = blob.thermal;
        memcpy(bandTime, blob.bandTime, sizeof(bandTime));
        totalSamples = blob.totalSamples;
        heaterCycles = blob.heaterCycles;
        diagUptimeSec = blob.diagUptimeSec;

        resetUnexpected = blob.resetUnexpected;
        resetWdt = blob.resetWdt;
        resetBrownout = blob.resetBrownout;

        sensorDisconnectCount = blob.sensorDisconnectCount;

        overCount = blob.overCount;
        overSum = blob.overSum;
        overMax = blob.overMax;
        memcpy(overHist, blob.overHist, sizeof(overHist));

        sensorDelta.n = blob.deltaN;
        sensorDelta.sum = blob.deltaSum;
        sensorDelta.max = blob.deltaMax;

        heaterOnTotalMs = blob.heaterOnTotalMs;
        onTimeSumMs = blob.onTimeSumMs;
        onTimeCount = blob.onTimeCount;
        invalidReads = blob.invalidReads;
        memcpy(tempHist, blob.tempHist, sizeof(tempHist));

        memcpy(dayDeltaAvg, blob.dayDeltaAvg, sizeof(dayDeltaAvg));
        memcpy(dayDuty, blob.dayDuty, sizeof(dayDuty));
        memcpy(dayOverAvg, blob.dayOverAvg, sizeof(dayOverAvg));
        memcpy(dayMeanDev, blob.dayMeanDev, sizeof(dayMeanDev));
        memcpy(dayHealth, blob.dayHealth, sizeof(dayHealth));
        dayIdx = blob.dayIdx;
        lastDayStart = blob.lastDayStart;
        // Estimate filled days without expanding DiagBlob layout
        dayCountFilled = 0;
        for (int di = 0; di < 30; di++) {
            if (dayHealth[di] != 0 || dayDuty[di] != 0.0f) dayCountFilled++;
        }
        if (dayCountFilled < dayIdx) dayCountFilled = dayIdx;
        if (dayCountFilled > 30) dayCountFilled = 30;

        // Restore last-8 faults; use persisted faultCount when available
        uint8_t restored = 0;
        for (int i = 0; i < 8; i++) {
            if (blob.savedFaults[i].ts != 0 || blob.savedFaults[i].code != 0) {
                uint8_t idx = restored % FAULT_RING_SIZE;
                faultRing[idx] = blob.savedFaults[i];
                restored++;
            }
        }
        uint32_t storedCount = prefDiag.getULong("faultCount", restored);
        if (storedCount > FAULT_RING_SIZE) storedCount = FAULT_RING_SIZE;
        // Prefer the larger of restored slots vs stored count (both are lower bounds)
        faultCount = (storedCount > restored) ? storedCount : restored;
        if (faultCount > FAULT_RING_SIZE) faultCount = FAULT_RING_SIZE;
        faultHead = (uint8_t)(faultCount % FAULT_RING_SIZE);
        blobDirty = false;
    } else {
        // ---- لا توجد بيانات سابقة → تهيئة من الصفر ----
        thermal.n = 0; thermal.mean = 0; thermal.m2 = 0; thermal.minT = 999; thermal.maxT = -999;
        for (int i = 0; i < 6; i++) bandTime[i] = 0;
        totalSamples = 0; heaterCycles = 0; diagUptimeSec = 0;
        resetUnexpected = 0; resetWdt = 0; resetBrownout = 0;
        sensorDisconnectCount = 0;
        overCount = 0; overSum = 0; overMax = 0; memset(overHist, 0, sizeof(overHist));
        sensorDelta.n = 0; sensorDelta.sum = 0; sensorDelta.max = 0;
        heaterOnTotalMs = 0; onTimeSumMs = 0; onTimeCount = 0; invalidReads = 0; memset(tempHist, 0, sizeof(tempHist));
        memset(dayDeltaAvg, 0, sizeof(dayDeltaAvg)); memset(dayDuty, 0, sizeof(dayDuty));
        memset(dayOverAvg, 0, sizeof(dayOverAvg)); memset(dayMeanDev, 0, sizeof(dayMeanDev));
        memset(dayHealth, 0, sizeof(dayHealth)); dayIdx = 0; dayCountFilled = 0; lastDayStart = millis();
        memset(faultRing, 0, sizeof(faultRing)); faultHead = 0; faultCount = 0;
        blobDirty = true;
    }

    // ---- قراءة سبب إعادة التشغيل وزيادة العدادات ----
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
        case ESP_RST_WDT: resetWdt++; blobDirty = true; addFault(FAULT_WDT_RESET, FAULT_CRITICAL, 0); break;
        case ESP_RST_BROWNOUT: resetBrownout++; blobDirty = true; addFault(FAULT_BROWNOUT_RESET, FAULT_CRITICAL, 0); break;
        case ESP_RST_POWERON: break;
        default: resetUnexpected++; blobDirty = true; break;
    }
    diagUptimeSec = prefDiag.getULong("diag_uptime", 0);


    lastSaveTime = millis();

    // FIX: تهيئة لقطة الدورة الحالية عند الإقلاع
    diag_cycleReset();
}
// ============================================================
// Phase 2: Getters for Overshoot and Delta
// ============================================================


void diag_sample()
{
    // Only sample if we have a valid temperature (rawTemperature is global)
    extern float rawTemperature;
    extern bool hasValidTemperature;
    extern bool heaterState;

    if (!hasValidTemperature)
        return;

    float temp = rawTemperature;
    // ============================================================
    // Phase 3.5: تتبع وقت تشغيل السخان (Duty Cycle & Avg ON Time)
    // ============================================================
    static bool lastHeaterStateForTiming = false;
    bool currentHeaterState = heaterState;

    // اكتشاف حافة ON (انتقال من OFF إلى ON)
    if (!lastHeaterStateForTiming && currentHeaterState)
    {
        currentOnStartMs = millis();
    }
    // اكتشاف حافة OFF (انتقال من ON إلى OFF)
    if (lastHeaterStateForTiming && !currentHeaterState)
    {
        if (currentOnStartMs != 0)
        {
            unsigned long onTime = millis() - currentOnStartMs;
            if (onTime > 100)
            { // تجاهل التشغيلات القصيرة جداً (أقل من 0.1 ثانية)
                onTimeSumMs += onTime;
                onTimeCount++;
            }
            currentOnStartMs = 0;
        }
    }
    // إذا كان السخان يعمل حالياً، أضف الوقت المنقضي منذ آخر تحديث إلى الإجمالي
    if (currentHeaterState)
    {
        // لا نضيف هنا مباشرة، بل نضيف في نهاية الدورة أو نستخدم طريقة أخرى.
        // الطريقة الأفضل: إضافة الوقت المنقضي بين القراءات (2 ثانية) إذا كان السخان يعمل.
        // Accumulate heater on-time using measured inter-sample deltaMs (not a fixed 2 s).
        // Sample interval baseline ~SENSOR_INTERVAL; actual on-time uses measured deltaMs below.
    }
    lastHeaterStateForTiming = currentHeaterState;

    // بدلاً من الطريقة أعلاه، سنستخدم طريقة مباشرة: إضافة وقت العينة (2 ثانية) إلى الإجمالي إذا كان السخان يعمل.
    // هذا أكثر دقة ويتوافق مع تردد العينات.
    // Actual inter-sample delta (always advance, for duty + bandTime)
    static unsigned long lastSampleMs = 0;
    unsigned long nowMs = millis();
    unsigned long deltaMs = (lastSampleMs == 0) ? 2000UL : (nowMs - lastSampleMs);
    if (deltaMs > 10000UL) deltaMs = 2000UL;  // clamp after long stall
    lastSampleMs = nowMs;
    if (currentHeaterState) {
        heaterOnTotalMs += deltaMs;
    }

    // ---- Update Welford ----
    thermal.n++;
    float delta = temp - thermal.mean;
    thermal.mean += delta / thermal.n;
    thermal.m2 += delta * (temp - thermal.mean);
    // ============================================================
    // Phase 3.5: تحديث رسم بياني الحرارة (P95)
    // ============================================================
    int bin = (int)((temp - 30.0f) / 0.1f);
    if (bin >= 0 && bin < 150)
    {
        tempHist[bin]++;
    }
    if (temp < thermal.minT)
        thermal.minT = temp;
    if (temp > thermal.maxT)
        thermal.maxT = temp;

    // ---- Update bandTime with actual delta ----
    float seconds = (float)deltaMs / 1000.0f;
    int band = getBandIndex(temp);
    if (band >= 0 && band < 6)
        bandTime[band] += seconds;

    // ---- Count total samples ----
    totalSamples++;

    // ---- Detect heater OFF transition (ON -> OFF) ----
    if (heaterWasOn && !heaterState)
    {
        heaterCycles++;
        blobDirty = true;
    }
    heaterWasOn = heaterState;

    // Mark dirty if any change occurred (we do it above already)
    // but we also set dirty if any new sample changes stats (always)
    static bool lastHeaterState = false;
    // bool currentHeaterState = heaterState;

    if (lastHeaterState && !currentHeaterState)
    {
        // Heater just turned OFF -> arm overshoot window
        overshoot.armed = true;
        overshoot.offMs = millis();
        overshoot.offTemp = rawTemperature;
        overshoot.peak = rawTemperature;
        overshoot.samples = 0;
    }
    lastHeaterState = currentHeaterState;

    // If armed and we have valid temp, track peak
    if (overshoot.armed && hasValidTemperature)
    {
        overshoot.samples++;
        if (rawTemperature > overshoot.peak)
        {
            overshoot.peak = rawTemperature;
        }
        // Check if window expired (120 seconds)
        if ((millis() - overshoot.offMs) >= 120000UL)
        {
            // Calculate overshoot
            if (overshoot.samples >= 3)
            {
                float overshootVal = overshoot.peak - overshoot.offTemp;
                if (overshootVal < 0)
                    overshootVal = 0;
                // Update running stats
                overCount++;
                overSum += overshootVal;
                if (overshootVal > overMax)
                    overMax = overshootVal;
                // Update histogram (bin width 0.05°C, max 1.0°C)
                int bin = (int)(overshootVal / 0.05f);
                if (bin > 19)
                    bin = 19;
                overHist[bin]++;
                blobDirty = true;
                // توليد خطأ إذا كان التجاوز مرتفعاً
                if (overshootVal > 0.4f)
                {
                    addFault(FAULT_OVERSHOOT_HIGH, FAULT_CRITICAL, overshootVal);
                }
            }
            overshoot.armed = false;
        }
    }

    // ============================================================
    // Phase 2: Sensor Delta (DS18B20 agreement)
    // ============================================================
    extern DSTemperatureSensor dsSensors[];
    if (dsSensors[0].valid && dsSensors[1].valid)
    {
        float d = fabsf(dsSensors[0].temperature - dsSensors[1].temperature);
        sensorDelta.n++;
        sensorDelta.sum += d;
        if (d > sensorDelta.max)
            sensorDelta.max = d;

        // ----- تحسين كشف فرق الحساسين (مع مهلة ورفع العتبة) -----
        static float deltaHistory[3] = {0.0f, 0.0f, 0.0f};
        static int deltaIdx = 0;

        deltaHistory[deltaIdx] = d;
        deltaIdx = (deltaIdx + 1) % 3;

        // التحقق من أن آخر 3 قراءات جميعها تجاوزت العتبة الجديدة 0.7 درجة
        float threshold = 0.7f;
        if (deltaHistory[0] > threshold && deltaHistory[1] > threshold && deltaHistory[2] > threshold)
        {
            addFault(FAULT_SENSOR_DELTA_HIGH, FAULT_CRITICAL, d);
        }
    }

    // Phase 3: Track sensor disconnections
    diag_trackSensorDisconnect();

    // ============================================================
    // Phase 3.5: عد القراءات غير الصالحة (Invalid reads)
    // ============================================================
    if (!dsSensors[0].valid || !dsSensors[1].valid)
    {
        invalidReads++;
    }

    // Mark dirty if any change occurred
    blobDirty = true;
}

    // Mark 

// ============================================================
// Phase 3: Fault Ring Functions
// ============================================================


void diag_periodic()
{
    // حساب الوقت الكلي بشكل تراكمي (وليس من الصفر)
    static unsigned long lastDiagTick = 0;
    unsigned long nowMillis = millis();
    
    if (lastDiagTick == 0) lastDiagTick = nowMillis;
    diagUptimeSec += (nowMillis - lastDiagTick) / 1000;
    lastDiagTick = nowMillis;

    // ✨ حفظ الوقت التراكمي بشكل دوري في الذاكرة
    if (blobDirty && (millis() - lastSaveTime >= SAVE_INTERVAL_MS)) {
        prefDiag.putULong("diag_uptime", diagUptimeSec);
        saveDiagnosticsBlob();
        lastSaveTime = millis();
    }

    // --- منطق اليوم (24 ساعة) ---
    if (lastDayStart == 0) lastDayStart = millis();
    if ((millis() - lastDayStart) >= 86400000UL) { // 24 ساعة
        // احفظ متوسطات اليوم المنتهي
        dayDeltaAvg[dayIdx] = getAvgDelta();
        dayDuty[dayIdx] = getDutyCycle();
        dayOverAvg[dayIdx] = getAvgOvershoot();
        dayMeanDev[dayIdx] = fabsf(thermal.mean - 37.7f);
        dayHealth[dayIdx] = (uint8_t)getHealthScore();
        
        dayIdx = (dayIdx + 1) % 30;
        if (dayCountFilled < 30) dayCountFilled++;
        lastDayStart = millis();
        blobDirty = true;
    }
    
    // حفظ البيانات إذا كانت متسخة ومر الوقت الكافي (فقط مرة واحدة، بدون تكرار)
    if (blobDirty && (millis() - lastSaveTime >= SAVE_INTERVAL_MS)) {
        saveDiagnosticsBlob();
        lastSaveTime = millis();
    }
}
// ============================================================
// 6. Health Score Calculation (Phase 3)
// ============================================================

