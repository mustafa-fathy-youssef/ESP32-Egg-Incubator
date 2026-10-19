// src/testing/sensor_test.cpp
// Comprehensive Sensor Testing & Profiling Package
// Authenticity, divergence, checksum, and peripheral bus health verification.

#include "sensor_test.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// External hardware references from main sketch / sensors module
extern OneWire oneWire;
extern DallasTemperature sensors;
extern DHT dht;

static int authScore = 100;
static int divergenceScore = 100;
static int dhtScore = 100;
static int i2cScore = 100;

void sensor_test_init() {
    Serial.println("🧪 Sensor Test Package Initialized");
}

void sensor_test_run_suite() {
    Serial.println("==================================================");
    Serial.println("🧪 RUNNING COMPREHENSIVE SENSOR & HARDWARE TEST SUITE");
    Serial.println("==================================================");

    // 1. DS18B20 Authenticity Test (Family Code 0x28)
    authScore = 100;
    int deviceCount = sensors.getDeviceCount();
    Serial.printf("🔍 DS18B20 Devices Found: %d\n", deviceCount);
    
    DeviceAddress devAddr;
    for (int i = 0; i < deviceCount; i++) {
        if (sensors.getAddress(devAddr, i)) {
            uint8_t familyCode = devAddr[0];
            Serial.printf("   - Sensor #%d ROM Family Code: 0x%02X ", i + 1, familyCode);
            if (familyCode == 0x28) {
                Serial.println("[✅ GENUINE DS18B20]");
            } else {
                Serial.println("[⚠️ UNKNOWN / CLONE CHIP]");
                authScore -= 25;
            }
        } else {
            Serial.printf("   - Sensor #%d: Read Address Failed\n", i + 1);
            authScore -= 50;
        }
    }
    if (deviceCount == 0) authScore = 0;
    if (authScore < 0) authScore = 0;

    // 2. DS18B20 Inter-Sensor Divergence Test
    divergenceScore = 100;
    sensors.requestTemperatures();
    if (deviceCount >= 2) {
        float t1 = sensors.getTempCByIndex(0);
        float t2 = sensors.getTempCByIndex(1);
        float delta = fabsf(t1 - t2);
        Serial.printf("⚖️ Inter-Sensor Divergence: T1=%.2f°C, T2=%.2f°C | Delta=%.2f°C\n", t1, t2, delta);
        if (delta > 0.5f) {
            Serial.println("   [⚠️ WARNING: Sensor divergence exceeds 0.5°C threshold!]");
            divergenceScore = 50;
        } else {
            Serial.println("   [✅ EXCELLENT: Sensors are closely matched]");
        }
    } else {
        Serial.println("   [ℹ️ Only 1 DS18B20 sensor active; divergence check skipped]");
    }

    // 3. DHT22 Humidity & Temp Test
    dhtScore = 100;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
        Serial.println("🌡️ DHT22 Sensor: [❌ FAILED / CHECKSUM ERROR / DISCONNECTED]");
        dhtScore = 0;
    } else {
        Serial.printf("🌡️ DHT22 Sensor: Humidity=%.1f%%, Temp=%.1f°C [✅ PASSED]\n", h, t);
        if (h < 0.0f || h > 100.0f || t < -40.0f || t > 80.0f) {
            Serial.println("   [⚠️ WARNING: Readings out of plausible range]");
            dhtScore = 50;
        }
    }

    // 4. I2C Bus & LCD Scan Test
    i2cScore = 100;
    byte error, address;
    int nDevices = 0;
    Serial.println("🔍 Scanning I2C Bus for LCD & Peripherals...");
    for (address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("   - I2C device found at address 0x%02X [✅ ACK]\n", address);
            nDevices++;
        }
    }
    if (nDevices == 0) {
        Serial.println("   [❌ No I2C devices acknowledged transmission]");
        i2cScore = 0;
    }

    // Summary Score
    int overall = sensor_test_get_overall_score();
    Serial.println("==================================================");
    Serial.printf("📊 SENSOR TEST SUITE COMPLETE — OVERALL HEALTH SCORE: %d / 100\n", overall);
    Serial.printf("   - Authenticity: %d/100\n   - Divergence: %d/100\n   - DHT22: %d/100\n   - I2C Bus: %d/100\n",
                  authScore, divergenceScore, dhtScore, i2cScore);
    Serial.println("==================================================");
}

int sensor_test_get_authenticity_score() { return authScore; }
int sensor_test_get_divergence_score() { return divergenceScore; }
int sensor_test_get_dht_score() { return dhtScore; }
int sensor_test_get_i2c_score() { return i2cScore; }
int sensor_test_get_overall_score() {
    return (authScore + divergenceScore + dhtScore + i2cScore) / 4;
}
