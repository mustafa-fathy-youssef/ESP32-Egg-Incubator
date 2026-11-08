#ifndef SYSTEMS_H
#define SYSTEMS_H

#include <Arduino.h>
#include "Sensors.h"

// ==========================================
// 1. BASE SYSTEM (Parent Class for Actuators)
// ==========================================
class ControlledSystem {
  protected:
    int outputPin;
    int indicatorPin;
    bool isActive;

  public:
    ControlledSystem(int outPin, int indPin = -1) {
      outputPin = outPin;
      indicatorPin = indPin;
      isActive = false;
    }

    virtual void begin() {
      pinMode(outputPin, OUTPUT);
      if (indicatorPin != -1) {
        pinMode(indicatorPin, OUTPUT);
      }
      turnOff();
    }

    virtual void turnOn() {
      isActive = true;
      digitalWrite(outputPin, HIGH);
      if (indicatorPin != -1) {
        digitalWrite(indicatorPin, HIGH);
      }
    }

    virtual void turnOff() {
      isActive = false;
      digitalWrite(outputPin, LOW);
      if (indicatorPin != -1) {
        digitalWrite(indicatorPin, LOW);
      }
    }

    bool getStatus() const {
      return isActive;
    }
};

// ==========================================
// 2. HEATING SYSTEM (Child Class)
// ==========================================
class HeatingSystem : public ControlledSystem {
  private:
    float targetMinTemp;
    float targetMaxTemp;
    TempRampTracker rampTracker;

  public:
    HeatingSystem(int outPin, int indPin, float minTemp = 24.5, float maxTemp = 25.5, float rampDelta = 1.0) 
      : ControlledSystem(outPin, indPin), rampTracker(rampDelta) {
      targetMinTemp = minTemp;
      targetMaxTemp = maxTemp;
    }

    void evaluate(float currentTemp) {
      // 1. Check if heating needed
      if (currentTemp < targetMinTemp) {
        if (!getStatus()) {
          rampTracker.startHeatingCycle(currentTemp);
          turnOn();
        }
      } 
      // 2. Check if target reached / exceeded
      else if (currentTemp > targetMaxTemp) {
        if (getStatus()) {
          rampTracker.stopHeatingCycle(currentTemp);
          turnOff();
        }
      }

      // 3. Update performance metrics (time to raise 1 degree, overshoot)
      rampTracker.update(currentTemp);
    }
};

// ==========================================
// 3. EVAPORATING / HUMIDITY SYSTEM (Child Class)
// ==========================================
class EvaporatingSystem : public ControlledSystem {
  private:
    float targetMinHumidity;
    float targetMaxHumidity;

  public:
    EvaporatingSystem(int outPin, int indPin, float minHum = 50.0, float maxHum = 60.0) 
      : ControlledSystem(outPin, indPin) {
      targetMinHumidity = minHum;
      targetMaxHumidity = maxHum;
    }

    void evaluate(float currentHumidity) {
      // 1. Check if humidification needed
      if (currentHumidity < targetMinHumidity) {
        if (!getStatus()) {
          Serial.println(F("[Evaporation] Humidity low. Turning ON evaporator."));
          turnOn();
        }
      } 
      // 2. Check if target humidity reached
      else if (currentHumidity > targetMaxHumidity) {
        if (getStatus()) {
          Serial.println(F("[Evaporation] Target humidity reached. Turning OFF evaporator."));
          turnOff();
        }
      }
    }
};

#endif
