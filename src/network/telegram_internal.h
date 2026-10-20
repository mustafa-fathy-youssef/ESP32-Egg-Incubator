#pragma once
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "secrets.h"
#include "diagnostics.h"
#include "telegram_alerts.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

struct ResetStats {
    uint32_t totalCount;
    uint32_t lastCount;
    uint32_t lastReasonCode;
    unsigned long lastBootTime;
    unsigned long lastDay;
    uint32_t dailyCount;
    uint32_t wdtCount;
    uint32_t panicCount;
    uint32_t brownoutCount;
    uint32_t poweronCount;
    uint32_t otherCount;
};

extern char last_reset_reason[80];
extern ResetStats resetStats;
extern bool monitorEnabled;
extern unsigned long lastMonitorMs;
extern UniversalTelegramBot bot;
extern WiFiClientSecure secured_client;

bool tgSend(const char* text);
bool syncTime();
String getTimeString();
void saveLog(const DetailedLog& log);
void saveResetStats();
void updateResetStats();

// [FIX] externs from telegram_alerts.cpp used by telegram_bot.cpp + telegram_storage.cpp
extern const char* ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;
extern struct tm timeinfo;
extern Preferences prefLog;
extern Preferences prefReset;
extern bool monitorEnabled;
extern unsigned long lastMonitorMs;

#ifndef DEBOUNCE_RING
#define DEBOUNCE_RING 4
#endif

// [FIX] from telegram_alerts.cpp - used by telegram_bot.cpp
extern bool lastEmergState;
extern bool bootReportSent;
extern unsigned long lastDailyReportDay;
extern uint8_t recentCodes[DEBOUNCE_RING];
extern unsigned long recentTimes[DEBOUNCE_RING];
extern uint8_t recentIdx;

// [FIX] prototypes
bool tgSendOnce(const char* text);
void sendBootResetReport();

// [FIX] global variables used by telegram_bot.cpp
extern bool emergencyMode;
extern bool safeMode;
extern float rawTemperature;
extern float currentTemperature;
extern bool hasValidTemperature;
extern bool heaterState;
extern bool turnerState;
extern unsigned long elapsedSec;
extern uint8_t bootCount;
extern bool wifiConnected;
extern DSTemperatureSensor dsSensors[];
