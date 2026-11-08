#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Enum to tell the DHT22 what to read
enum DHTType { READ_TEMP, READ_HUMIDITY };

// ==========================================
// 1. THE PARENT CLASS (Generic)
// ==========================================
class GenericSensor {
  protected:
    int numReads;
    static const int MAX_READS = 10; 
    float readings[MAX_READS];
    int readIndex;
    float total;
    float minValidValue;
    float maxValidValue;

  public:
    GenericSensor(int _numReads = 5, float _min = 0.0, float _max = 100.0);
    
    virtual float readRaw() = 0;
    float getSmoothedReading();
    void begin(float fallbackValue = 50.0);
};

// ==========================================
// 2. THE SUB-CLASSES
// ==========================================
class DS18B20Sensor : public GenericSensor {
  private:
    OneWire oneWire;
    DallasTemperature sensors;
  public:
    DS18B20Sensor(int pin, int _numReads = 5);
    void begin();
    float readRaw() override;
};

class DHT22Sensor : public GenericSensor {
  private:
    DHT dht;
    DHTType readType;
  public:
    DHT22Sensor(int pin, DHTType type, int _numReads = 5);
    void begin();
    float readRaw() override;
};

// ==========================================
// 3. TEMP RAMP TRACKER (Incubator Performance)
// ==========================================
class TempRampTracker {
  private:
    bool isHeating;
    float startTemp;
    float targetTemp;
    unsigned long startTime;
    float peakTemp;
    unsigned long targetReachedTime;
    float targetDelta; // e.g., 1.0 or 2.0 °C

  public:
    TempRampTracker(float delta = 1.0) {
      isHeating = false;
      startTemp = 0.0;
      targetTemp = 0.0;
      startTime = 0;
      peakTemp = 0.0;
      targetReachedTime = 0;
      targetDelta = delta;
    }

    void startHeatingCycle(float currentTemp) {
      if (!isHeating) {
        isHeating = true;
        startTemp = currentTemp;
        targetTemp = currentTemp + targetDelta;
        startTime = millis();
        peakTemp = currentTemp;
        targetReachedTime = 0;
        Serial.print(F("[RampTracker] Heating started. Start: "));
        Serial.print(startTemp);
        Serial.print(F(" °C | Target (+"));
        Serial.print(targetDelta);
        Serial.print(F("°C): "));
        Serial.print(targetTemp);
        Serial.println(F(" °C"));
      }
    }

    void update(float currentTemp) {
      if (!isHeating) return;

      // Track peak temp for overshoot calculation
      if (currentTemp > peakTemp) {
        peakTemp = currentTemp;
      }

      // Check if target reached (logged only once per cycle)
      if (targetReachedTime == 0 && currentTemp >= targetTemp) {
        targetReachedTime = millis();
        unsigned long durationMs = targetReachedTime - startTime;
        float secondsPerDegree = (durationMs / 1000.0) / targetDelta;

        Serial.println(F("=== HEATING PERFORMANCE REPORT ==="));
        Serial.print(F("Time to raise "));
        Serial.print(targetDelta);
        Serial.print(F(" °C: "));
        Serial.print(durationMs / 1000.0);
        Serial.print(F(" seconds ("));
        Serial.print(secondsPerDegree);
        Serial.println(F(" sec/°C)"));
        Serial.print(F("Start: ")); Serial.print(startTemp);
        Serial.print(F(" °C -> Target Reached: ")); Serial.print(currentTemp); Serial.println(F(" °C"));
      }
    }

    void stopHeatingCycle(float currentTemp) {
      if (isHeating) {
        // Update peak one last time
        if (currentTemp > peakTemp) peakTemp = currentTemp;

        if (targetReachedTime != 0) {
          float overshoot = peakTemp - targetTemp;
          if (overshoot < 0) overshoot = 0;
          Serial.print(F("Peak Temp Reached: "));
          Serial.print(peakTemp);
          Serial.print(F(" °C | Overshoot: +"));
          Serial.print(overshoot);
          Serial.println(F(" °C"));
        } else {
          Serial.println(F("[RampTracker] Heater turned off before reaching target."));
        }
        Serial.println(F("=== END OF CYCLE ==="));
        isHeating = false;
      }
    }

    bool active() const { return isHeating; }
};

#endif
