#pragma once
#include "telegram_alerts.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

struct ResetStats {
    uint32_t count;
    uint32_t expectedCount;
    uint32_t unexpectedCount;
    uint32_t wdtCount;
    uint32_t brownoutCount;
    uint32_t safeModeCount;
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
