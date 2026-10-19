#include "diagnostics_internal.h"

float getAvgOvershoot()
{
    return (overCount > 0) ? (overSum / overCount) : 0.0f;
}

float getMaxOvershoot()
{
    return overMax;
}

float getP95Overshoot()
{
    // Compute P95 from histogram (20 bins, width 0.05)
    uint32_t total = 0;
    for (int i = 0; i < 20; i++)
        total += overHist[i];
    if (total == 0)
        return 0.0f;

    uint32_t cum = 0;
    uint32_t target = (uint32_t)(total * 0.95f);
    for (int i = 0; i < 20; i++)
    {
        cum += overHist[i];
        if (cum >= target)
        {
            return (float)(i + 1) * 0.05f; // upper edge of bin
        }
    }
    return 1.0f; // > 1.0°C
}

float getAvgDelta()
{
    return (sensorDelta.n > 0) ? (sensorDelta.sum / sensorDelta.n) : 0.0f;
}

float getMaxDelta()
{
    return sensorDelta.max;
}

// ============================================================
// Phase 3: Track Sensor Disconnections
// ============================================================
void diag_trackSensorDisconnect()
{
    extern DSTemperatureSensor dsSensors[];
    for (int i = 0; i < 2; i++)
    {
        bool currentlyDisconnected = !dsSensors[i].valid;
        if (!lastSensorDisconnectState[i] && currentlyDisconnected)
        {
            sensorDisconnectCount++;
            prefDiag.putULong("sensorDisconn", sensorDisconnectCount);
            blobDirty = true;
            addFault(FAULT_SENSOR_DISCONNECT, FAULT_WARNING, i);
            Serial.printf("📊 Sensor[%d] disconnected! Total disconnects: %lu\n", i, sensorDisconnectCount);
        }
        lastSensorDisconnectState[i] = currentlyDisconnected;
    }
}
// ============================================================
// Phase 3.5: Getters للإضافات المتقدمة
// ============================================================

float getP95Temp()
{
    uint32_t total = 0;
    for (int i = 0; i < 150; i++)
        total += tempHist[i];
    if (total == 0)
        return 0.0f;

    uint32_t cum = 0;
    uint32_t target = (uint32_t)(total * 0.95f);
    for (int i = 0; i < 150; i++)
    {
        cum += tempHist[i];
        if (cum >= target)
        {
            return 30.0f + (float)(i + 1) * 0.1f; // الحافة العليا للخانة
        }
    }
    return 45.0f;
}

uint32_t getInvalidReads()
{
    return invalidReads;
}

float getDutyCycle()
{
    // إذا لم يمر وقت كافٍ، ارجع 0
    if (diagUptimeSec == 0)
        return 0.0f;

    float totalOnSec = (float)heaterOnTotalMs / 1000.0f;
    float duty = (totalOnSec / (float)diagUptimeSec) * 100.0f;
    
    // حماية من الأخطاء المنطقية
    if (duty > 100.0f) duty = 100.0f;
    if (duty < 0.0f)   duty = 0.0f;
    
    return duty;
}
float getAvgOnTime()
{
    if (onTimeCount == 0)
        return 0.0f;
    return ((float)onTimeSumMs / (float)onTimeCount) / 1000.0f; // بالثواني
}

// ============================================================
// FIX (Phase 7 precision): إحصائيات خاصة بالدورة الحالية فقط
// بدلاً من القيم التراكمية منذ الإقلاع التي كانت تُمرَّر سابقاً
// إلى cycle_archive_onCycleComplete() - ما كان يجعل قيم الدورات
// المتتالية شبه متطابقة (لأنها لقطات من نفس المتوسط التراكمي
// البطيء)، ويُضعف فعلياً كشف التدهور بين الدورات.
// ============================================================
void diag_cycleReset()
{
    cycleBaselineHeaterOnMs = heaterOnTotalMs;
    cycleBaselineUptimeSec  = diagUptimeSec;
    cycleBaselineOverCount  = overCount;
    cycleBaselineOverSum    = overSum;
}

float getCycleDutyCycle()
{
    uint32_t deltaOnMs = heaterOnTotalMs - cycleBaselineHeaterOnMs;
    unsigned long deltaUptimeSec = diagUptimeSec - cycleBaselineUptimeSec;
    if (deltaUptimeSec == 0)
        return 0.0f;
    return ((float)deltaOnMs / 1000.0f) / (float)deltaUptimeSec * 100.0f;
}

float getCycleAvgOvershoot()
{
    uint32_t deltaCount = overCount - cycleBaselineOverCount;
    float deltaSum = overSum - cycleBaselineOverSum;
    return (deltaCount > 0) ? (deltaSum / deltaCount) : 0.0f;
}

// ============================================================
// 4. diag_sample – call once per sensor cycle (every 2s)

float getHealthScore()
{
    // المكونات الستة، كل منها يبدأ بـ 100 ثم تنقص العقوبات
    float thermalScore = 100.0f;
    float overshootScore = 100.0f;
    float sensorScore = 100.0f;
    float heaterScore = 100.0f;
    float softwareScore = 100.0f;
    float actuatorScore = 100.0f;  // <-- تعريف واحد فقط

    // ----- 1. Thermal (penalties from diagnostics.h) -----
    float meanDev = fabsf(thermal.mean - 37.7f);
    if (meanDev > 0.3f)
        thermalScore -= PENALTY_MEAN_DEV_HIGH;
    else if (meanDev > 0.15f)
        thermalScore -= PENALTY_MEAN_DEV_MED;

    float spread = thermal.maxT - thermal.minT;
    if (spread > 0.6f)
        thermalScore -= PENALTY_SPREAD_HIGH;
    else if (spread > 0.4f)
        thermalScore -= PENALTY_SPREAD_MED;

    float totalBand = 0;
    for (int i = 0; i < 6; i++)
        totalBand += bandTime[i];
    if (totalBand > 0)
    {
        float idealPct = (bandTime[3] / totalBand) * 100.0f;
        if (idealPct < 80.0f)
            thermalScore -= PENALTY_IDEAL_BAND_LOW;
        else if (idealPct < 90.0f)
            thermalScore -= PENALTY_IDEAL_BAND_MED;
    }

    // ----- 2. Overshoot -----
    float avgOver = getAvgOvershoot();
    float p95Over = getP95Overshoot();
    if (p95Over > 0.4f)
        overshootScore -= PENALTY_OVERSHOOT_P95_HIGH;
    else if (p95Over > 0.25f)
        overshootScore -= PENALTY_OVERSHOOT_P95_MED;
    else if (avgOver > 0.2f)
        overshootScore -= PENALTY_OVERSHOOT_AVG;

    // ----- 3. Sensors -----
    float avgDelta = getAvgDelta();
    float maxDelta = getMaxDelta();
    if (maxDelta > 0.5f)
        sensorScore -= PENALTY_DELTA_MAX_HIGH;
    else if (maxDelta > 0.3f)
        sensorScore -= PENALTY_DELTA_MAX_MED;
    else if (avgDelta > 0.2f)
        sensorScore -= PENALTY_DELTA_AVG;

    if (sensorDisconnectCount > 0)
    {
        sensorScore -= min(PENALTY_SENSOR_DISCONNECT_CAP,
                           (float)sensorDisconnectCount * PENALTY_SENSOR_DISCONNECT_STEP);
    }

    // ----- 4. Heater (M8: عقوبة حسب معدل دورات/ساعة وليس مجرد >0) -----
    if (diagUptimeSec >= 3600UL && heaterCycles > 0) {
        float cph = (float)heaterCycles / ((float)diagUptimeSec / 3600.0f);
        if (cph > 120.0f) heaterScore -= PENALTY_HEATER_CYCLES * 2.0f;
        else if (cph > 60.0f) heaterScore -= PENALTY_HEATER_CYCLES;
        // أقل من 60 دورة/ساعة: لا عقوبة دائمة
    } else if (diagUptimeSec < 3600UL && heaterCycles > 200) {
        heaterScore -= PENALTY_HEATER_CYCLES;  // حماية مبكرة فقط عند نشاط مفرط
    }

    // ----- 5. Software -----
    if (resetUnexpected > 0)
        softwareScore -= PENALTY_RESET_UNEXPECTED * resetUnexpected;
    if (resetWdt > 0)
        softwareScore -= PENALTY_RESET_WDT * resetWdt;
    if (resetBrownout > 0)
        softwareScore -= PENALTY_RESET_BROWNOUT * resetBrownout;

    // ----- 6. Actuators (0=Fan, 1=Turner, 2=Heater, 3=Evaporator) -----
    if (diagSwitchCount[2] > 500) actuatorScore -= PENALTY_HEATER_HIGH_SWITCH;
    else if (diagSwitchCount[2] > 300) actuatorScore -= PENALTY_HEATER_MED_SWITCH;
    if (diagSwitchCount[0] < 10 && diagUptimeSec > 3600) actuatorScore -= PENALTY_FAN_LOW_SWITCH;
    if (diagUptimeSec > 0) {
        float duty = (float)diagOnMs[2] / (diagUptimeSec * 1000.0f);
        if (duty > 0.5f) actuatorScore -= PENALTY_HEATER_HIGH_DUTY;
    }
    
    if (actuatorScore < 0) actuatorScore = 0;

    // ---- الأوزان ----
    float weights[6] = {0.30f, 0.20f, 0.15f, 0.10f, 0.15f, 0.10f};
    float scores[6] = {thermalScore, overshootScore, sensorScore, heaterScore, softwareScore, actuatorScore};

    float totalScore = 0;
    for (int i = 0; i < 6; i++)
    {
        if (scores[i] < 0)
            scores[i] = 0;
        if (scores[i] > 100)
            scores[i] = 100;
        totalScore += scores[i] * weights[i];
    }

    // ---- القاعدة الإجبارية ----
    uint32_t nowSec = millis() / 1000;
    bool hasCritical = false;
    for (int i = 0; i < FAULT_RING_SIZE; i++)
    {
        if (faultRing[i].level >= FAULT_CRITICAL)
        {
            if (nowSec - faultRing[i].ts < 86400UL)
            {
                hasCritical = true;
                break;
            }
        }
    }
    if (hasCritical)
    {
        if (totalScore > 40.0f)
            totalScore = 40.0f;
    }

    return totalScore;
}
const char *getHealthClass()
{
    float score = getHealthScore();
    if (score >= 85.0f)
        return "EXCELLENT";
    else if (score >= 70.0f)
        return "GOOD";
    else if (score >= 50.0f)
        return "WARNING";
    else
        return "CRITICAL";
}

// ============================================================
// مسح جميع بيانات التشخيص والبدء من الصفر

float getDegradationSlope() {
    // dayCountFilled: number of valid daily samples (0..30)
    // While filling: days are stored at indices 0 .. dayCountFilled-1 in time order.
    // When full: dayIdx is the next write slot = oldest sample; chronological
    // order is dayIdx, dayIdx+1, ... (mod 30).
    int n = (int)dayCountFilled;
    if (n < 3) return 0.0f;

    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    for (int i = 0; i < n; i++) {
        float x = (float)i;
        uint8_t idx;
        if (n < 30) {
            idx = (uint8_t)i;                 // sequential fill from 0
        } else {
            idx = (uint8_t)((dayIdx + i) % 30); // oldest → newest
        }
        float y = (float)dayHealth[idx];
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }
    float denom = (float)n * sumX2 - sumX * sumX;
    if (denom > -1e-6f && denom < 1e-6f) return 0.0f;
    float slope = ((float)n * sumXY - sumX * sumY) / denom;
    return slope; // negative = degradation
}

const char* getDegradationStatus() {
    float slope = getDegradationSlope();
    if (slope < DEGRADATION_THRESHOLD) return "DEGRADATION DETECTED";
    else return "NOT DETECTED";
}