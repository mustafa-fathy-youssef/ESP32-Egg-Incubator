#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================
// 1. THE PARENT CLASS (Production Ready)
// ==========================================
class TemperatureSensor {
  protected:
    int numReads;
    static const int MAX_READS = 10; 
    float readings[MAX_READS];
    int readIndex;
    float total;
    float minValidTemp;
    float maxValidTemp;

  public:
    TemperatureSensor(int _numReads = 5, float _minT = -55.0, float _maxT = 125.0) {
      numReads = (_numReads > MAX_READS) ? MAX_READS : _numReads;
      readIndex = 0;
      total = 0.0;
      minValidTemp = _minT;
      maxValidTemp = _maxT;
      for (int i = 0; i < numReads; i++) readings[i] = 0.0;
    }

    virtual float readRaw() = 0;

    float getSmoothedReading() {
      float newReading = readRaw();

      // 1. Catch disconnections (NaN)
      if (isnan(newReading)) {
        return total / numReads; // Return last average
      }

      // 2. Catch Ghost Spikes (Out of bounds)
      if (newReading < minValidTemp || newReading > maxValidTemp) {
        return total / numReads; // Ignore the spike
      }

      // 3. Smoothing Logic
      total = total - readings[readIndex]; 
      readings[readIndex] = newReading;    
      total = total + readings[readIndex]; 
      readIndex = (readIndex + 1) % numReads;

      return total / (float)numReads;
    }
    
    void begin() {
      float initialRead = readRaw();
      // If the first read is invalid, use a safe default
      if (isnan(initialRead) || initialRead < minValidTemp || initialRead > maxValidTemp) initialRead = 25.0; 
      for (int i = 0; i < numReads; i++) {
        readings[i] = initialRead;
        total += initialRead;
      }
    }
};

// ==========================================
// 2. THE SUB-CLASSES
// ==========================================

class DS18B20Sensor : public TemperatureSensor {
  private:
    OneWire oneWire;
    DallasTemperature sensors;
  public:
    DS18B20Sensor(int pin, int _numReads = 5) : TemperatureSensor(_numReads, -55.0, 125.0), oneWire(pin), sensors(&oneWire) {}

    void begin() {
      sensors.begin();
      TemperatureSensor::begin();
    }

    float readRaw() override {
      sensors.requestTemperatures();
      return sensors.getTempCByIndex(0);
    }
};

// ==========================================
// 3. THE MAIN SKETCH (Non-blocking)
// ==========================================

DS18B20Sensor roomSensor(4);      
const int heaterPin = 1;
const int heaterLed = 2;

// Timing variables (Replaces delay)
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL_MS = 1000; 

void setup() {
  Serial.begin(115200);
  pinMode(heaterPin, OUTPUT);
  pinMode(heaterLed, OUTPUT);
  digitalWrite(heaterPin, LOW);
  digitalWrite(heaterLed, LOW);

  roomSensor.begin();
  Serial.println("System Initialized. Heater OFF.");
}

void loop() {
  // NON-BLOCKING TIMER: Only read once per second
  if (millis() - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = millis();

    float smoothedTemp = roomSensor.getSmoothedReading();

    // PRODUCTION FIX: LED mirrors the heater state
    if (smoothedTemp < 24.5) {
      digitalWrite(heaterPin, HIGH);
      digitalWrite(heaterLed, HIGH);
    } else if (smoothedTemp > 25.5) {
      digitalWrite(heaterPin, LOW);
      digitalWrite(heaterLed, LOW);
    }

    // PRODUCTION FIX: Print for debugging, but not every single loop
    Serial.print("Temp: ");
    Serial.println(smoothedTemp);
  }
  
  // The ESP32 can now do other things here (check Wi-Fi, buttons, etc.)
}
