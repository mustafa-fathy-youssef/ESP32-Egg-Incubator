#include "Sensors.h"

// ==========================================
// GenericSensor (Parent Class) Implementation
// ==========================================
GenericSensor::GenericSensor(int _numReads, float _min, float _max) {
  numReads = (_numReads > MAX_READS) ? MAX_READS : _numReads;
  readIndex = 0;
  total = 0.0;
  minValidValue = _min;
  maxValidValue = _max;
  for (int i = 0; i < numReads; i++) readings[i] = 0.0;
}

float GenericSensor::getSmoothedReading() {
  float newReading = readRaw();

  // 1. Catch disconnections (NaN)
  if (isnan(newReading)) {
    return total / numReads; 
  }

  // 2. Catch Ghost Spikes (Out of bounds)
  if (newReading < minValidValue || newReading > maxValidValue) {
    return total / numReads; 
  }

  // 3. Smoothing Logic
  total = total - readings[readIndex]; 
  readings[readIndex] = newReading;    
  total = total + newReading; 
  readIndex = (readIndex + 1) % numReads;

  return total / (float)numReads;
}

void GenericSensor::begin(float fallbackValue) {
  float initialRead = readRaw();
  if (isnan(initialRead) || initialRead < minValidValue || initialRead > maxValidValue) {
    initialRead = fallbackValue; 
  }
  total = 0.0; 
  for (int i = 0; i < numReads; i++) {
    readings[i] = initialRead;
    total += initialRead;
  }
}

// ==========================================
// DS18B20Sensor (Child) Implementation
// ==========================================
DS18B20Sensor::DS18B20Sensor(int pin, int _numReads) 
  : GenericSensor(_numReads, -55.0, 125.0), 
    oneWire(pin), sensors(&oneWire) {}

void DS18B20Sensor::begin() {
  sensors.begin();
  GenericSensor::begin(25.0); // 25.0 C fallback
}

float DS18B20Sensor::readRaw() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

// ==========================================
// DHT22Sensor (Child) Implementation
// ==========================================
DHT22Sensor::DHT22Sensor(int pin, DHTType type, int _numReads) 
  : GenericSensor(_numReads, (type == READ_TEMP ? -40.0 : 0.0), (type == READ_TEMP ? 80.0 : 100.0)), 
    dht(pin, DHT22) {
  readType = type;
}

void DHT22Sensor::begin() {
  dht.begin();
  float fallback = (readType == READ_TEMP) ? 25.0 : 50.0;
  GenericSensor::begin(fallback); 
}

float DHT22Sensor::readRaw() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (readType == READ_TEMP) return t;
  else return h;
}
