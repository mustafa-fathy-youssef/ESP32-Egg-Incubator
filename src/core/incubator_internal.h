#pragma once

#ifndef PIN_ONE_WIRE
#define PIN_ONE_WIRE 4
#endif
#ifndef PIN_DHT
#define PIN_DHT 15
#endif
#ifndef PIN_FAN
#define PIN_FAN 13
#endif
#ifndef PIN_TURNER
#define PIN_TURNER 14
#endif
#ifndef PIN_HEATER
#define PIN_HEATER 27
#endif
#ifndef PIN_SENSOR_FAILED
#define PIN_SENSOR_FAILED 5
#endif
#ifndef PIN_EVAPORATOR
#define PIN_EVAPORATOR 12
#endif

#ifndef MAX_DS18B20_SENSORS
#define MAX_DS18B20_SENSORS 2
#endif
#ifndef MAX_HUMIDITY_SENSORS
#define MAX_HUMIDITY_SENSORS 1
#endif
#ifndef ACTUATOR_COUNT
#define ACTUATOR_COUNT 4
#endif

#ifndef DHT_RETRY_INTERVAL
#define DHT_RETRY_INTERVAL 60000
#endif
#ifndef DHT_TYPE
#define DHT_TYPE DHT22
#endif
#ifndef TEMP_SMOOTHING_ALPHA
#define TEMP_SMOOTHING_ALPHA 0.25f
#endif
#ifndef RELAY_STUCK_MARGIN
#define RELAY_STUCK_MARGIN 2.0f
#endif
#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT 30
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "2.0.1"
#endif
#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME "ESP32 Egg Incubator"
#endif

#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <Wire.h>
#include "diag_io.h"
#include "diagnostics.h"
#include "telegram_alerts.h"

struct SystemConfig {
    float targetTemp;
    float tempLow;
    float tempHigh;
    float tempCritical;
    float tempEmergency;
    unsigned long turnerOffTime;
    unsigned long turnerOnTime;
    unsigned long heaterMaxRuntime;
    unsigned long nvsSaveInterval;
    void setDefaultConfig();
};

extern SystemConfig config;
extern Preferences preferences;

extern OneWire oneWire;
extern DallasTemperature ds18b20;
extern LiquidCrystal_I2C lcd;

extern float rawTemperature;
extern bool smootherInitialized;
extern float currentTemperature;
extern bool hasValidTemperature;
extern bool avgValid;
extern bool safeMode;
extern bool emergencyMode;
extern unsigned long emergencyStartTime;
extern unsigned long emergencyStableSince;
extern unsigned long safeStableSince;
extern int sensorFailCounter;
extern unsigned long relayStuckTimer;
extern bool relayStuckFault;
extern float avgHumidity;
extern float cycle_temp_sum;
extern unsigned long cycleCount;
extern unsigned long lastMillis;
extern unsigned long elapsedSec;
extern bool heaterState;
extern unsigned long heaterOnStartTime;
extern bool heaterRuntimeAlert;
extern bool turnerState;
extern unsigned long lastTurnerTime;
extern bool lcdInitialized;
extern unsigned long lastSensorTime;

void loadConfig();
void saveConfig();
void setActuator(int index, bool state);
void forceAllHeatersOff();
void enterSafeMode(const char* reason);
void exitSafeMode();
void enterEmergencyMode();
void exitEmergencyMode();
void logEvent(const char* message);
bool isPlausibleTemperature(float temp);
bool isTemperatureJumpValid(float temp, float lastTemp);
float applyTempSmoothing(float newRawTemp);
void checkRelayStuckFault();
void initSensors();
void resetHumiditySensor(int index);
void controlEvaporator();
void readSensors();
void reinitLCD();
void updateLCD();
void controlHeater();
void checkHeaterRuntime();
void checkAutomaticSafetyExit();
void updateTimer();
void handleSerialCommands();
void changeState(uint8_t newState);
void printStateChange(uint8_t newState);

// [FIX] FSM state (defined in incubator_config.cpp)
typedef enum {
    STATE_BOOT = 0,
    STATE_NORMAL,
    STATE_SAFE,
    STATE_EMERGENCY
} SystemState;

extern SystemState currentState;
extern unsigned long stateEnterMs;
extern const char* currentStateName;

// [FIX] globals used by ESP32_Incubator.ino
extern uint8_t bootCount;
extern bool waitingForConfirm;
extern unsigned long confirmTimeout;
extern char serialBuffer[64];
extern uint8_t serialIdx;
extern uint32_t cycle_sample_count;
extern Preferences pref;
extern Preferences prefConfig;

// [FIX] evaporator (defined in incubator_config.cpp)
struct EvaporatorController {
    bool state;
    unsigned long lastToggleTime;
    unsigned long onDuration;
    unsigned long offDuration;
    bool useSafeTimedMode;
};
extern EvaporatorController evaporator;

// [FIX] hardware arrays
extern int dhtPins[MAX_HUMIDITY_SENSORS];
extern HumiditySensor humiditySensors[MAX_HUMIDITY_SENSORS];
extern Actuator actuators[ACTUATOR_COUNT];
extern DSTemperatureSensor dsSensors[MAX_DS18B20_SENSORS];

// [FIX] prototypes
void setDefaultConfig();
void setupWatchdog();
