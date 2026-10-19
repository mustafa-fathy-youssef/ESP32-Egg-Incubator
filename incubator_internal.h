#pragma once
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
