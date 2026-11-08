#include <iostream>
#include <cmath>
#include <cassert>
#include <vector>

// --- Arduino Mocks ---
unsigned long mock_millis = 0;
unsigned long millis() { return mock_millis; }
void advance_time(unsigned long ms) { mock_millis += ms; }

bool pin_states[100] = {false};
void pinMode(int pin, int mode) {}
void digitalWrite(int pin, int val) { pin_states[pin] = (val == 1); }
int digitalRead(int pin) { return pin_states[pin] ? 1 : 0; }

#define HIGH 1
#define LOW 0
#define OUTPUT 1

bool isnan_val(float x) { return std::isnan(x); }
#define isnan isnan_val

// --- Core Classes Replicated for Test Validation ---

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
    GenericSensor(int _numReads = 5, float _min = 0.0, float _max = 100.0) {
      numReads = (_numReads > MAX_READS) ? MAX_READS : _numReads;
      readIndex = 0;
      total = 0.0;
      minValidValue = _min;
      maxValidValue = _max;
      for (int i = 0; i < numReads; i++) readings[i] = 0.0;
    }
    
    virtual float readRaw() = 0;

    float getSmoothedReading() {
      float newReading = readRaw();
      if (isnan(newReading)) return total / numReads; 
      if (newReading < minValidValue || newReading > maxValidValue) return total / numReads; 

      total = total - readings[readIndex]; 
      readings[readIndex] = newReading;    
      total = total + newReading; 
      readIndex = (readIndex + 1) % numReads;

      return total / (float)numReads;
    }

    void begin(float fallbackValue = 50.0) {
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
};

class MockSensor : public GenericSensor {
  private:
    float currentRaw;
  public:
    MockSensor(float initialVal) : GenericSensor(5, -55.0, 125.0) {
      currentRaw = initialVal;
    }
    void setRaw(float val) { currentRaw = val; }
    float readRaw() override { return currentRaw; }
};

class TempRampTracker {
  private:
    bool isHeating;
    float startTemp;
    float targetTemp;
    unsigned long startTime;
    float peakTemp;
    unsigned long targetReachedTime;
    float targetDelta;

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
      }
    }

    void update(float currentTemp) {
      if (!isHeating) return;
      if (currentTemp > peakTemp) peakTemp = currentTemp;
      if (targetReachedTime == 0 && currentTemp >= targetTemp) {
        targetReachedTime = millis();
      }
    }

    void stopHeatingCycle(float currentTemp) {
      if (isHeating) {
        if (currentTemp > peakTemp) peakTemp = currentTemp;
        isHeating = false;
      }
    }

    unsigned long getDurationMs() const { return targetReachedTime - startTime; }
    float getPeakTemp() const { return peakTemp; }
    bool active() const { return isHeating; }
};

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
      if (indicatorPin != -1) pinMode(indicatorPin, OUTPUT);
      turnOff();
    }

    virtual void turnOn() {
      isActive = true;
      digitalWrite(outputPin, HIGH);
      if (indicatorPin != -1) digitalWrite(indicatorPin, HIGH);
    }

    virtual void turnOff() {
      isActive = false;
      digitalWrite(outputPin, LOW);
      if (indicatorPin != -1) digitalWrite(indicatorPin, LOW);
    }

    bool getStatus() const { return isActive; }
};

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
    TempRampTracker& getTracker() { return rampTracker; }
};

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
      if (currentHumidity < targetMinHumidity) {
        if (!getStatus()) turnOn();
      } else if (currentHumidity > targetMaxHumidity) {
        if (getStatus()) turnOff();
      }
    }
};

// --- Test Suite Execution ---
void test_sensor_smoothing() {
  std::cout << "[TEST] Running test_sensor_smoothing..." << std::endl;
  MockSensor sensor(24.0);
  sensor.begin();

  sensor.setRaw(25.0);
  float s1 = sensor.getSmoothedReading();
  assert(s1 > 24.0 && s1 <= 25.0);

  sensor.setRaw(200.0); // Invalid spike
  float s2 = sensor.getSmoothedReading();
  assert(s2 < 50.0); // Should ignore spike

  std::cout << "[PASS] test_sensor_smoothing passed successfully." << std::endl;
}

void test_temp_ramp_tracker() {
  std::cout << "[TEST] Running test_temp_ramp_tracker..." << std::endl;
  TempRampTracker tracker(1.0); // Track 1.0 °C rise

  mock_millis = 1000;
  tracker.startHeatingCycle(24.0);
  assert(tracker.active() == true);

  advance_time(5000);
  tracker.update(24.5);
  
  advance_time(5000);
  tracker.update(25.0); // Target reached (+1.0 °C)

  assert(tracker.getDurationMs() == 10000); // 10 seconds

  tracker.stopHeatingCycle(25.2);
  assert(tracker.active() == false);
  assert(tracker.getPeakTemp() == 25.2);

  std::cout << "[PASS] test_temp_ramp_tracker passed successfully." << std::endl;
}

void test_heating_and_evaporating_systems() {
  std::cout << "[TEST] Running test_heating_and_evaporating_systems..." << std::endl;
  
  HeatingSystem heater(1, 2, 24.5, 25.5, 1.0);
  EvaporatingSystem evaporator(3, 4, 50.0, 60.0);

  heater.begin();
  evaporator.begin();

  heater.evaluate(24.0);
  assert(heater.getStatus() == true);

  advance_time(10000);
  heater.evaluate(26.0);
  assert(heater.getStatus() == false);

  evaporator.evaluate(45.0);
  assert(evaporator.getStatus() == true);

  evaporator.evaluate(65.0);
  assert(evaporator.getStatus() == false);

  std::cout << "[PASS] test_heating_and_evaporating_systems passed successfully." << std::endl;
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "    INCUBATOR SYSTEM UNIT TEST SUITE    " << std::endl;
  std::cout << "========================================" << std::endl;
  test_sensor_smoothing();
  test_temp_ramp_tracker();
  test_heating_and_evaporating_systems();
  std::cout << "========================================" << std::endl;
  std::cout << "    ALL UNIT TESTS PASSED SUCCESSFULLY  " << std::endl;
  std::cout << "========================================" << std::endl;
  return 0;
}
