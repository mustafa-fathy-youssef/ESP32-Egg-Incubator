#include "Sensors.h"
#include "Systems.h"

// 1. Sensors
DS18B20Sensor roomTempDS(4);               // DS18B20 temp on Pin 4
DHT22Sensor roomTempDHT(15, READ_TEMP);    // DHT22 temp on Pin 15
DHT22Sensor roomHumidityDHT(15, READ_HUMIDITY); // DHT22 humidity on Pin 15

// 2. Actuator Systems (Inheriting from ControlledSystem)
// HeatingSystem(heaterPin, heaterLedPin, minTemp, maxTemp, rampDelta)
HeatingSystem incubatorHeater(1, 2, 24.5, 25.5, 1.0);

// EvaporatingSystem(evapPin, evapLedPin, minHumidity, maxHumidity)
EvaporatingSystem incubatorEvaporator(3, 5, 50.0, 60.0);

// Non-blocking timer variables
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL_MS = 2000; 

void setup() {
  Serial.begin(115200);

  // Initialize all actuator systems
  incubatorHeater.begin();
  incubatorEvaporator.begin();

  // Initialize sensors
  roomTempDS.begin();
  roomTempDHT.begin();
  roomHumidityDHT.begin();

  Serial.println(F("Egg Incubator Control System Initialized."));
}

void loop() {
  // Non-blocking timer
  if (millis() - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = millis();

    // 1. Read sensors (with smoothing)
    float tDS = roomTempDS.getSmoothedReading();
    float tDHT = roomTempDHT.getSmoothedReading();
    float hDHT = roomHumidityDHT.getSmoothedReading();

    // 2. Debug telemetry
    Serial.print(F("Temp (DS18B20): ")); Serial.print(tDS); Serial.print(F(" °C | "));
    Serial.print(F("Humidity (DHT22): ")); Serial.print(hDHT); Serial.println(F(" %"));

    // 3. Evaluate and run Heating System (tracks time to raise 1°C & overshoot)
    incubatorHeater.evaluate(tDS);

    // 4. Evaluate and run Evaporating System
    incubatorEvaporator.evaluate(hDHT);
  }

  // Non-blocking execution allows handling Wi-Fi, displays, buttons here
}
