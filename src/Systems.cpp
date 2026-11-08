#include "Systems.h"

// ==========================================
// ControlledSystem (Parent Class) Implementation
// ==========================================
ControlledSystem::ControlledSystem(int outPin, int indPin) {
  outputPin = outPin;
  indicatorPin = indPin;
  isActive = false;
}

void ControlledSystem::begin() {
  pinMode(outputPin, OUTPUT);
  if (indicatorPin != -1) {
    pinMode(indicatorPin, OUTPUT);
  }
  turnOff();
}

void ControlledSystem::turnOn() {
  isActive = true;
  digitalWrite(outputPin, HIGH);
  if (indicatorPin != -1) {
    digitalWrite(indicatorPin, HIGH);
  }
}

void ControlledSystem::turnOff() {
  isActive = false;
  digitalWrite(outputPin, LOW);
  if (indicatorPin != -1) {
    digitalWrite(indicatorPin, LOW);
  }
}

bool ControlledSystem::getStatus() const {
  return isActive;
}

// ==========================================
// HeatingSystem (Child Class) Implementation
// ==========================================
HeatingSystem::HeatingSystem(int outPin, int indPin, float minTemp, float maxTemp, float rampDelta) 
  : ControlledSystem(outPin, indPin), rampTracker(rampDelta) {
  targetMinTemp = minTemp;
  targetMaxTemp = maxTemp;
}

void HeatingSystem::evaluate(float currentTemp) {
  if (currentTemp < targetMinTemp) {
    if (!getStatus()) {
      rampTracker.startHeatingCycle(currentTemp);
      turnOn();
    }
  } else if (currentTemp > targetMaxTemp) {
    if (getStatus()) {
      rampTracker.stopHeatingCycle(currentTemp);
      turnOff();
    }
  }
  rampTracker.update(currentTemp);
}

TempRampTracker& HeatingSystem::getTracker() {
  return rampTracker;
}

// ==========================================
// EvaporatingSystem (Child Class) Implementation
// ==========================================
EvaporatingSystem::EvaporatingSystem(int outPin, int indPin, float minHum, float maxHum) 
  : ControlledSystem(outPin, indPin) {
  targetMinHumidity = minHum;
  targetMaxHumidity = maxHum;
}

void EvaporatingSystem::evaluate(float currentHumidity) {
  if (currentHumidity < targetMinHumidity) {
    if (!getStatus()) {
      Serial.println(F("[Evaporation] Humidity low. Turning ON evaporator."));
      turnOn();
    }
  } else if (currentHumidity > targetMaxHumidity) {
    if (getStatus()) {
      Serial.println(F("[Evaporation] Target humidity reached. Turning OFF evaporator."));
      turnOff();
    }
  }
}
