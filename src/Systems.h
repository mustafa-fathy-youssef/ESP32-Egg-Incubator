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
    ControlledSystem(int outPin, int indPin = -1);
    virtual void begin();
    virtual void turnOn();
    virtual void turnOff();
    bool getStatus() const;
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
    HeatingSystem(int outPin, int indPin, float minTemp = 24.5, float maxTemp = 25.5, float rampDelta = 1.0);
    void evaluate(float currentTemp);
    TempRampTracker& getTracker();
};

// ==========================================
// 3. EVAPORATING / HUMIDITY SYSTEM (Child Class)
// ==========================================
class EvaporatingSystem : public ControlledSystem {
  private:
    float targetMinHumidity;
    float targetMaxHumidity;

  public:
    EvaporatingSystem(int outPin, int indPin, float minHum = 50.0, float maxHum = 60.0);
    void evaluate(float currentHumidity);
};

#endif
