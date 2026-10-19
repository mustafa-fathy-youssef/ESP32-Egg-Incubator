#include "diagnostics_internal.h"

void addFault(uint8_t code, uint8_t level, float value)
{
    faultRing[faultHead].code = code;
    faultRing[faultHead].level = level;
    faultRing[faultHead].ts = millis() / 1000; // uptime seconds
    faultRing[faultHead].value = value;
    faultHead = (faultHead + 1) % FAULT_RING_SIZE;
    if (faultCount < FAULT_RING_SIZE)
        faultCount++;
    blobDirty = true;
    // حفظ فوري في NVS عند حدوث CRITICAL أو EMERGENCY
    if (level >= FAULT_CRITICAL)
    {
        saveDiagnosticsBlob(); // حفظ فوري

        prefDiag.putULong("faultHead", faultHead);
        prefDiag.putULong("faultCount", faultCount);
        // saveFaultsToNVS();
    }
    DetailedLog log;
    log.id = nextLogId;
    log.type = LOG_TYPE_FAULT;
    snprintf(log.reason, sizeof(log.reason), "Fault 0x%02X", code);
    strcpy(log.timestamp, getTimeString().c_str());
    log.uptimeSec = millis() / 1000;
    log.value = value;
    log.code = code;
    saveLog(log);


}
// ============================================================
// حفظ واستعادة البيانات كنسخة مزدوجة مع CRC
// ============================================================
void saveDiagnosticsBlob() {
    // 1. ملء الهيكل بالبيانات الحالية
    blob.magic = 0xABADBABE;
    blob.version = 1;
    blob.timestamp = millis();

    blob.thermal = thermal;
    memcpy(blob.bandTime, bandTime, sizeof(bandTime));
    blob.totalSamples = totalSamples;
    blob.heaterCycles = heaterCycles;
    blob.diagUptimeSec = diagUptimeSec;

    blob.resetUnexpected = resetUnexpected;
    blob.resetWdt = resetWdt;
    blob.resetBrownout = resetBrownout;

    blob.sensorDisconnectCount = sensorDisconnectCount;

    blob.overCount = overCount;
    blob.overSum = overSum;
    blob.overMax = overMax;
    memcpy(blob.overHist, overHist, sizeof(overHist));

    blob.deltaN = sensorDelta.n;
    blob.deltaSum = sensorDelta.sum;
    blob.deltaMax = sensorDelta.max;

    blob.heaterOnTotalMs = heaterOnTotalMs;
    blob.onTimeSumMs = onTimeSumMs;
    blob.onTimeCount = onTimeCount;
    blob.invalidReads = invalidReads;
    memcpy(blob.tempHist, tempHist, sizeof(tempHist));

    memcpy(blob.dayDeltaAvg, dayDeltaAvg, sizeof(dayDeltaAvg));
    memcpy(blob.dayDuty, dayDuty, sizeof(dayDuty));
    memcpy(blob.dayOverAvg, dayOverAvg, sizeof(dayOverAvg));
    memcpy(blob.dayMeanDev, dayMeanDev, sizeof(dayMeanDev));
    memcpy(blob.dayHealth, dayHealth, sizeof(dayHealth));
    blob.dayIdx = dayIdx;
    blob.lastDayStart = lastDayStart;

    // نسخ آخر 8 أعطال من الحلقة الدائرية
    uint8_t start = (faultHead >= 8) ? (faultHead - 8) : 0;
    for (int i = 0; i < 8; i++) {
        uint8_t idx = (start + i) % FAULT_RING_SIZE;
        blob.savedFaults[i] = faultRing[idx];
    }

    // 2. حساب CRC على الهيكل (عدا حقل CRC نفسه)
    size_t crcOffset = offsetof(DiagBlob, crc);
    blob.crc = calculateCRC16((uint8_t*)&blob + crcOffset + sizeof(uint16_t), 
                              sizeof(DiagBlob) - crcOffset - sizeof(uint16_t));

    // 3. الكتابة في النسخة غير النشطة
    uint8_t writeIdx = (activeBlobIdx == 0) ? 1 : 0;
    prefDiag.putBytes(BLOB_KEYS[writeIdx], (uint8_t*)&blob, sizeof(DiagBlob));
    
    // 4. تأكيد الكتابة بتبديل المؤشر
    activeBlobIdx = writeIdx;
    prefDiag.putUChar("activeIdx", activeBlobIdx);
    
    blobDirty = false;
    // Serial.println("💾 Dual‑blob saved successfully");
}

bool loadDiagnosticsBlob() {
    // قراءة المؤشر النشط المحفوظ
    activeBlobIdx = prefDiag.getUChar("activeIdx", 0);
    
    // محاولة تحميل النسخة النشطة أولاً
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t idx = (activeBlobIdx + attempt) % 2;
        size_t len = prefDiag.getBytesLength(BLOB_KEYS[idx]);
        if (len != sizeof(DiagBlob)) continue;

        DiagBlob temp;
        prefDiag.getBytes(BLOB_KEYS[idx], (uint8_t*)&temp, sizeof(DiagBlob));

        if (temp.magic != 0xABADBABE || temp.version != 1) continue;

        // التحقق من CRC
        size_t crcOffset = offsetof(DiagBlob, crc);
        uint16_t calcCrc = calculateCRC16((uint8_t*)&temp + crcOffset + sizeof(uint16_t),
                                          sizeof(DiagBlob) - crcOffset - sizeof(uint16_t));
        if (temp.crc != calcCrc) continue;

        // البيانات صحيحة → نسخها إلى المتغيرات العامة
        memcpy(&blob, &temp, sizeof(DiagBlob));
        activeBlobIdx = idx;
        return true;
    }
    return false; // لا توجد نسخة صالحة
}
// void saveFaultsToNVS()
// {
//     // نحفظ آخر 8 أحداث فقط لتوفير المساحة
//     uint8_t start = (faultHead >= 8) ? (faultHead - 8) : 0;
//     for (int i = 0; i < 8; i++)
//     {
//         uint8_t idx = (start + i) % FAULT_RING_SIZE;
//         char key[8];
//         snprintf(key, sizeof(key), "f%dc", i);
//         prefDiag.putUChar(key, faultRing[idx].code);
//         snprintf(key, sizeof(key), "f%dl", i);
//         prefDiag.putUChar(key, faultRing[idx].level);
//         snprintf(key, sizeof(key), "f%dt", i);
//         prefDiag.putULong(key, faultRing[idx].ts);
//         snprintf(key, sizeof(key), "f%dv", i);
//         prefDiag.putFloat(key, faultRing[idx].value);
//     }
// }

// void restoreFaultsFromNVS()
// {
//     for (int i = 0; i < 8; i++)
//     {
//         char key[8];
//         snprintf(key, sizeof(key), "f%dc", i);
//         if (prefDiag.isKey(key))
//         {
//             uint8_t idx = (faultHead + i) % FAULT_RING_SIZE;
//             faultRing[idx].code = prefDiag.getUChar(key, 0);
//             snprintf(key, sizeof(key), "f%dl", i);
//             faultRing[idx].level = prefDiag.getUChar(key, 0);
//             snprintf(key, sizeof(key), "f%dt", i);
//             faultRing[idx].ts = prefDiag.getULong(key, 0);
//             snprintf(key, sizeof(key), "f%dv", i);
//             faultRing[idx].value = prefDiag.getFloat(key, 0.0f);
//         }
//     }
// }

// ============================================================
// 5. diag_periodic – call every ~10 seconds from loop()
// ============================================================
// ============================================================
// 5. diag_periodic – call every ~10 seconds from loop()

void clearAllDiagnostics()
{
    // ----- 1. تصفير المتغيرات الإحصائية -----
    thermal.n = 0;
    thermal.mean = 0.0f;
    thermal.m2 = 0.0f;
    thermal.minT = 999.0f;
    thermal.maxT = -999.0f;

    for (int i = 0; i < 6; i++) bandTime[i] = 0.0f;
    dayCountFilled = 0;
    totalSamples = 0;
    heaterCycles = 0;
    diagUptimeSec = 0;

    resetUnexpected = 0;
    resetWdt = 0;
    resetBrownout = 0;

    sensorDisconnectCount = 0;
    lastSensorDisconnectState[0] = false;
    lastSensorDisconnectState[1] = false;

    overCount = 0;
    overSum = 0.0f;
    overMax = 0.0f;
    memset(overHist, 0, sizeof(overHist));
    overshoot.armed = false;
    overshoot.offMs = 0;
    overshoot.offTemp = 0.0f;
    overshoot.peak = 0.0f;
    overshoot.samples = 0;

    sensorDelta.n = 0;
    sensorDelta.sum = 0.0f;
    sensorDelta.max = 0.0f;

    heaterOnTotalMs = 0;
    onTimeSumMs = 0;
    onTimeCount = 0;
    currentOnStartMs = 0;

    invalidReads = 0;
    memset(tempHist, 0, sizeof(tempHist));

    faultHead = 0;
    faultCount = 0;
    memset(faultRing, 0, sizeof(faultRing));

    // ----- 2. مسح مساحة NVS بالكامل -----
    prefDiag.clear();

    // ----- 3. إزالة مفاتيح الـ blob يدوياً (للتأكد) -----
    prefDiag.remove(BLOB_KEYS[0]);
    prefDiag.remove(BLOB_KEYS[1]);
    prefDiag.remove("activeIdx");
    prefDiag.remove("faultHead");
    prefDiag.remove("faultCount");

        // تصفير عدادات المشغلات
    for (int i = 0; i < 4; i++) {
        diagSwitchCount[i] = 0;
        diagOnMs[i] = 0;
        lastOnStart[i] = 0;
    }

    // ----- 4. إعادة ضبط المؤشرات -----
    activeBlobIdx = 0;
    blobDirty = true;
    lastSaveTime = millis();

    // ----- 5. حفظ الحالة النظيفة (blob فارغ) -----
    saveDiagnosticsBlob();

    Serial.println("🗑️ All diagnostics data cleared! Starting fresh.");
    Serial.println("📊 Type 'REPORT' to see the new clean state.");
}// 7. Print functions
// ============================================================
