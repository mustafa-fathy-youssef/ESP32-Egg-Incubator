#include "diagnostics_internal.h"

void printFullReport()
{
    Serial.println("\n========== SMART INCUBATOR RELIABILITY REPORT ==========");

    // Uptime
    unsigned long uptimeSec = diagUptimeSec;
    unsigned long days = uptimeSec / 86400;
    unsigned long hours = (uptimeSec % 86400) / 3600;
    unsigned long mins = (uptimeSec % 3600) / 60;
    unsigned long secs = uptimeSec % 60;
    Serial.printf("Uptime: %lud %02luh %02lum %02lus\n", days, hours, mins, secs);
    Serial.printf("Samples: %lu\n", totalSamples);
    Serial.printf("Heater Cycles: %lu\n", heaterCycles);
    extern unsigned long cycleCount;
    Serial.printf("Cycle Count: %lu\n", cycleCount);

    // في قسم SENSOR HEALTH أضف:
    Serial.printf("  Avg Humidity: %.1f %%\n", avgHumidity);
    Serial.printf("  Humidity Fails: %lu\n", humidityFailCount);

    // Thermal stats
    float variance = (thermal.n > 1) ? (thermal.m2 / thermal.n) : 0.0f;
    float stddev = sqrtf(variance);
    Serial.printf("\nTHERMAL PERFORMANCE\n");
    Serial.printf("  Avg: %.2f °C\n", thermal.mean);
    Serial.printf("  Min: %.2f  Max: %.2f\n", thermal.minT, thermal.maxT);
    Serial.printf("  Std Dev: %.3f °C\n", stddev);
    Serial.printf("  Spread: %.2f °C\n", thermal.maxT - thermal.minT);
    Serial.printf("  P95: %.2f °C\n", getP95Temp());
    // Band distribution
    float totalBandTime = 0.0f;
    for (int i = 0; i < 6; i++)
        totalBandTime += bandTime[i];
    if (totalBandTime > 0.0f)
    {
        float idealPct = (bandTime[3] / totalBandTime) * 100.0f;
        Serial.printf("  Time in Ideal band (37.5-38.0): %.1f %%\n", idealPct);
    }
    else
    {
        Serial.printf("  Time in Ideal band: N/A\n");
    }

    // 📌 THERMAL OVERSHOOT (Phase 2)
    Serial.printf("  Overshoot Avg: %.2f °C\n", getAvgOvershoot());
    Serial.printf("  Overshoot Max: %.2f °C\n", getMaxOvershoot());
    Serial.printf("  Overshoot P95: %.2f °C\n", getP95Overshoot());

    // SENSOR HEALTH (Phase 2 – Sensor Delta)
    Serial.printf("\nSENSOR HEALTH\n");
    Serial.printf("  Avg Delta: %.2f °C\n", getAvgDelta());
    Serial.printf("  Max Delta: %.2f °C\n", getMaxDelta());
    Serial.printf("  Invalid reads: %lu\n", getInvalidReads());
    Serial.printf("  Sensor Disconnects: %lu\n", sensorDisconnectCount);

    // HEATER PERFORMANCE (Phase 2 – Duty Cycle سيضاف لاحقاً)
    Serial.printf("\nHEATER PERFORMANCE\n");
    Serial.printf("  Duty Cycle: %.1f %%\n", getDutyCycle());
    Serial.printf("  Avg ON Time: %.1f s\n", getAvgOnTime());
    // Software reliability
    Serial.printf("\nSOFTWARE RELIABILITY\n");
    Serial.printf("  Unexpected Resets: %lu\n", resetUnexpected);
    Serial.printf("  WDT Resets: %lu\n", resetWdt);
    Serial.printf("  Brownout Resets: %lu\n", resetBrownout);

    // Faults (Phase 3)
    Serial.printf("\nFAULT HISTORY\n");
    uint32_t critCount = 0, warnCount = 0, infoCount = 0;
    for (int i = 0; i < FAULT_RING_SIZE; i++)
    {
        if (faultRing[i].level == FAULT_CRITICAL)
            critCount++;
        else if (faultRing[i].level == 1)
            warnCount++;
        else if (faultRing[i].level == 0)
            infoCount++;
    }
        // ACTUATOR ACTIVITY (إضافة جديدة لتكامل المعيار)
    Serial.printf("\nACTUATOR ACTIVITY\n");
    Serial.printf("  Fan Switching: %lu\n", diagSwitchCount[0]);
    Serial.printf("  Turner Ops: %lu\n", diagSwitchCount[1]);
    Serial.printf("  Heater Switching: %lu  Avg ON Time: %.1f s\n",
                  diagSwitchCount[2],
                  (diagOnMs[2] > 0 && diagSwitchCount[2] > 0)
                      ? (diagOnMs[2] / (float)diagSwitchCount[2]) / 1000.0f : 0.0f);
    Serial.printf("  Evaporator Ops: %lu\n", diagSwitchCount[3]);
    Serial.printf("  Critical: %lu  Warnings: %lu  Info: %lu\n", critCount, warnCount, infoCount);

    // Risk assessment (Power and PCB unknown)
    Serial.printf("\nRISK ASSESSMENT\n");
    Serial.printf("  Thermal: LOW (est.)\n");
    Serial.printf("  Sensor: LOW (est.)\n");
    Serial.printf("  Software: LOW (est.)\n");
    Serial.printf("  Actuator: UNKNOWN\n");
    Serial.printf("  Power: UNKNOWN\n");
    Serial.printf("  PCB Thermal: UNKNOWN\n");

    // Overall Health (Phase 3)
    float score = getHealthScore();
    const char *cls = getHealthClass();
    Serial.printf("\nSYSTEM HEALTH: %.1f / 100\n", score);
    Serial.printf("HEALTH CLASS: %s\n", cls);

    if (diagUptimeSec >= 86400UL)
    {
        Serial.printf("CONFIDENCE: HIGH (sufficient data)\n");
    }
    else if (diagUptimeSec >= 3600UL)
    {
        Serial.printf("CONFIDENCE: MEDIUM\n");
    }
    else
    {
        Serial.printf("CONFIDENCE: LOW (insufficient data)\n");
    }
    Serial.printf("DEGRADATION: %s\n", getDegradationStatus());
    if (score >= 85.0f)
    {
        Serial.printf("OVERALL: PASS — CONTINUE OPERATION\n");
    }
    else if (score >= 50.0f)
    {
        Serial.printf("OVERALL: MONITOR — INVESTIGATE SOON\n");
    }
    else
    {
        Serial.printf("OVERALL: FAIL — INTERVENE IMMEDIATELY\n");
    }
    Serial.printf("RECOMMENDATION: %s\n", (score >= 85.0f) ? "CONTINUE OPERATION" : "INVESTIGATE ISSUES");
    Serial.println("========================================================\n");
}

void printHealthSummary()
{
    float score = getHealthScore();
    const char *cls = getHealthClass();
    Serial.printf("HEALTH: %.1f/100 | Class: %s | Uptime: %lu s\n", score, cls, diagUptimeSec);
}

void printDiagStatus()
{
    Serial.printf("DIAGNOSTICS STATUS (Phase 3)\n");
    Serial.printf("  totalSamples: %lu\n", totalSamples);
    Serial.printf("  heaterCycles: %lu\n", heaterCycles);
    Serial.printf("  thermal.n: %lu, mean: %.2f, min: %.2f, max: %.2f\n",
                  thermal.n, thermal.mean, thermal.minT, thermal.maxT);
    Serial.printf("  bandTime: ");
    for (int i = 0; i < 6; i++)
    {
        Serial.printf("%.1f ", bandTime[i]);
    }
    Serial.println();
    Serial.printf("  reset counters: unexpected=%lu, wdt=%lu, brownout=%lu\n",
                  resetUnexpected, resetWdt, resetBrownout);
    Serial.printf("  sensorDisconnects: %lu\n", sensorDisconnectCount);
    Serial.printf("  blobDirty=%d, lastSave=%lu\n", blobDirty, lastSaveTime);    Serial.printf("  faultCount: %lu, faultHead: %u\n", faultCount, faultHead);
}
void printFaultRing() {
    Serial.println("===== FAULT RING (last 16 events) =====");
    uint8_t start = (faultHead >= 16) ? (faultHead - 16) : 0;
    int printed = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t idx = (start + i) % FAULT_RING_SIZE;
        if (faultRing[idx].ts == 0 && faultRing[idx].code == 0) continue; // فارغ
        Serial.printf("#%d: code=0x%02X level=%d ts=%lu sec value=%.2f\n",
            i+1, faultRing[idx].code, faultRing[idx].level,
            faultRing[idx].ts, faultRing[idx].value);
        printed++;
    }
    if (printed == 0) {
        Serial.println("  (No faults recorded)");
    }
    Serial.printf("Total faults in ring: %lu, Head: %u\n", faultCount, faultHead);
}
// ============================================================
// كشف التدهور (Degradation) باستخدام الميل الخطي
