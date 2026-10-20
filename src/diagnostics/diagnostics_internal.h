#pragma once
#include "diagnostics.h"
#include <Preferences.h>
#include <esp_system.h>
#include "telegram_alerts.h"
#include <math.h>
#include <string.h>

struct ThermalStats {
    uint32_t n;
    float mean;
    float m2;
    float minT;
    float maxT;
};

#pragma pack(push, 1)
struct DiagBlob {
    uint32_t magic;          // 0xABADBABE
    uint32_t version;        // 1
    uint32_t timestamp;      // وقت الحفظ (millis)
    uint16_t crc;            // CRC16 للتحقق

    ThermalStats thermal;
    float bandTime[6];
    uint32_t totalSamples;
    uint32_t heaterCycles;
    uint32_t diagUptimeSec;

    uint32_t resetUnexpected;
    uint32_t resetWdt;
    uint32_t resetBrownout;

    uint32_t sensorDisconnectCount;

    uint32_t overCount;
    float overSum;
    float overMax;
    uint32_t overHist[20];

    uint32_t deltaN;
    float deltaSum;
    float deltaMax;

    uint32_t heaterOnTotalMs;
    uint32_t onTimeSumMs;
    uint32_t onTimeCount;
    uint32_t invalidReads;
    uint32_t tempHist[150];

    float dayDeltaAvg[30];
    float dayDuty[30];
    float dayOverAvg[30];
    float dayMeanDev[30];
    uint8_t dayHealth[30];
    uint8_t dayIdx;
    uint8_t dayCountFilled;
    uint32_t lastDayStart;

    FaultRec savedFaults[8];
};
#pragma pack(pop)

struct HeaterOvershoot {
    bool armed;
    unsigned long offMs;
    float offTemp;
    float peak;
    uint32_t samples;
};

struct SensorDelta {
    uint32_t n;
    float sum;
    float max;
};

// Global shared state for diagnostics modules
extern float dayDeltaAvg[30];
extern float dayDuty[30];
extern float dayOverAvg[30];
extern float dayMeanDev[30];
extern uint8_t dayHealth[30];
extern uint8_t dayIdx;
extern uint8_t dayCountFilled;
extern unsigned long lastDayStart;

extern uint32_t diagSwitchCount[4];
extern uint32_t diagOnMs[4];
extern unsigned long lastOnStart[4];

extern ThermalStats thermal;
extern HeaterOvershoot overshoot;
extern SensorDelta sensorDelta;
extern DiagBlob blob;
extern bool blobDirty;
extern uint8_t activeBlobIdx;
extern unsigned long lastSaveTime;
extern const char* BLOB_KEYS[2];
extern Preferences prefDiag;
extern uint32_t faultCount;
extern bool lastSensorDisconnectState[2];
extern uint16_t calculateCRC16(const uint8_t* data, size_t len);

bool loadDiagnosticsBlob();
void saveDiagnosticsBlob();
