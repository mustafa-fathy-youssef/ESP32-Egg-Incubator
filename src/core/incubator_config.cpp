#include "incubator_internal.h"

void setDefaultConfig() {
    config.targetTemp        = 37.7f;
    config.tempLow           = 37.4f;
    config.tempHigh          = 37.8f;
    config.tempCritical      = 39.5f;
    config.tempEmergency     = 39.8f;
    config.turnerOffTime     = 60000UL;
    config.turnerOnTime      = 15000UL;
    config.heaterMaxRuntime  = 30000UL;
    config.nvsSaveInterval   = 60UL;
}
// ============================================================
// ============================================================
// Finite State Machine – Safety-Critical Design
// ============================================================
// SystemState defined in incubator_internal.h

SystemState currentState = STATE_BOOT;
unsigned long stateEnterMs = 0;
const char* currentStateName = "BOOT";

void printStateChange(uint8_t newState) {          // ← غيّرنا لـ uint8_t
    const char* names[] = {"BOOT", "NORMAL", "SAFE", "EMERGENCY"};
    if (newState > 3) newState = 0;
    currentStateName = names[newState];
    Serial.printf("🔄 STATE → %s (uptime %lu s)\n", currentStateName, millis() / 1000UL);
}

void changeState(uint8_t newState) {               // ← غيّرنا لـ uint8_t
    if (currentState == (SystemState)newState) return;
    currentState = (SystemState)newState;
    stateEnterMs = millis();
    printStateChange(newState);
}
extern unsigned long cycleCount; // مُعرَّف في الأسفل

// ---- إدارة حساسات الرطوبة (قابلة للتوسيع) ----
#define MAX_HUMIDITY_SENSORS 1   // حالياً واحد، يمكن زيادتها
#define DHT_SENSOR_COUNT MAX_HUMIDITY_SENSORS  // أو احذف DHT_SENSOR_COUNT تماماً
int dhtPins[MAX_HUMIDITY_SENSORS] = {PIN_DHT};
// FIX: تمت إزالة تعريف struct HumiditySensor المكرر هنا - أصبح
// يُستورد من diagnostics.h (المصدر الوحيد الآن، مُضمَّن عبر
// diag_io.h) بدلاً من نسخة محلية منفصلة.
HumiditySensor humiditySensors[MAX_HUMIDITY_SENSORS];

// ---- إدارة المرطب (Evaporator) ----
// EvaporatorController defined in incubator_internal.h

EvaporatorController evaporator = {
    false, 0, 300000UL, 600000UL, false
};
////////////////////////////
bool waitingForConfirm = false;
unsigned long confirmTimeout = 0;

// ---- FIX: تمت إزالة تعريف struct Actuator المكرر هنا - أصبح
// يُستورد من diagnostics.h (المصدر الوحيد الآن).

// DSTemperatureSensor: defined once in diagnostics.h

// ========================================================================
// 3. تعريف المصفوفات والكائنات
// ========================================================================
#define PIN_EVAPORATOR 12

#define ACTUATOR_COUNT 4
// Index map (must match diagnostics.cpp diagSwitchCount / REPORT):
// 0=Fan, 1=Turner, 2=Heater, 3=Evaporator
Actuator actuators[ACTUATOR_COUNT] = {
    {PIN_FAN,     false, true,  "Fan",     false},
    {PIN_TURNER,  false, false, "Turner",  false},
    {PIN_HEATER,  false, false, "Heater",  true},
    {PIN_EVAPORATOR, false, false, "Evaporator", false}
};

// ---- حساسات DS18B20 ----
DSTemperatureSensor dsSensors[MAX_DS18B20_SENSORS];

// ---- حساسات DHT ----

// ---- كائنات المكتبات ----
LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(PIN_ONE_WIRE);
DallasTemperature ds18b20(&oneWire);
Preferences pref;

// ========================================================================
// 4. المتغيرات العامة
// ========================================================================

// ---- إدارة شاشة LCD ----
bool lcdInitialized = false;

// ---- المقلب (Turner) ----
unsigned long lastTurnerTime = 0;
bool turnerState = false;

// ---- السخان (Heater) ----
bool heaterState = false;
unsigned long heaterOnStartTime = 0;
bool heaterRuntimeAlert = false;

// ---- العد التنازلي ----
unsigned long elapsedSec = 0;
unsigned long lastMillis = 0;
unsigned long cycleCount = 0;

// ---- قراءة الحساسات ----
unsigned long lastSensorTime = 0;
float currentTemperature = 0;      // درجة الحرارة المستخدمة للتحكم
bool hasValidTemperature = false;
bool avgValid = false;

// ---- السلامة (Safety) ----
bool safeMode = false;
bool emergencyMode = false;
unsigned long emergencyStartTime = 0;
unsigned long emergencyStableSince = 0;              // ← جديد
unsigned long safeStableSince = 0;
int sensorFailCounter = 0;
const int MAX_SENSOR_FAIL = 3;

// ---- كشف عطل الريلاي ----
unsigned long relayStuckTimer = 0;
bool relayStuckFault = false;

// ---- إحصائيات الرطوبة للتشخيص (يتم ملؤها في readSensors) ----
float avgHumidity = 0.0f;
uint32_t humidityFailCount = 0;
uint32_t humiditySamples = 0;
// ---- Per-cycle temperature average (reset each cycle) ----
float    cycle_temp_sum     = 0.0f;
uint32_t cycle_sample_count = 0;
// ---- أوامر Serial ----
char serialBuffer[64] = "";
uint8_t serialIdx = 0;


