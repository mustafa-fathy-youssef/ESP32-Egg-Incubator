# Smart Egg Incubator Control System

An advanced, object-oriented, non-blocking Arduino firmware for smart egg incubators featuring robust sensor data smoothing, thermal ramp performance tracking (time-to-raise rate per degree & overshoot calculation), and isolated heating and evaporating control systems.

---

## 📁 Repository Structure

```text
SmoothingTemperature/
├── src/
│   ├── Sensors.h       # Abstract base sensor, DS18B20, DHT22, and TempRampTracker
│   ├── Sensors.cpp     # Sensor smoothing & raw reading implementations
│   ├── Systems.h       # ControlledSystem base class, HeatingSystem, EvaporatingSystem
│   └── Systems.cpp     # Actuator control & evaluation logic implementations
├── examples/
│   └── EggIncubator/
│       └── EggIncubator.ino # Main non-blocking orchestration sketch
├── tests/
│   └── test_incubator.cpp   # Automated C++ unit test suite (mocks & assertions)
├── README.md
└── LICENSE
```

---

## 🏛️ Architecture & OOP Design

1. **Sensors (`src/Sensors.h/.cpp`):**
   - **`GenericSensor`:** Abstract base class implementing sliding-window moving-average smoothing, NaN/disconnection catching, and ghost spike rejection.
   - **`DS18B20Sensor`:** Digital 1-Wire temperature sensor subclass.
   - **`DHT22Sensor`:** Digital temperature and humidity sensor subclass.
   - **`TempRampTracker`:** Independent helper class tracking time taken to raise temperature by 1°C/2°C and peak temperature overshoot.

2. **Systems (`src/Systems.h/.cpp`):**
   - **`ControlledSystem`:** Generic parent class for hardware actuators managing pins, states (`turnOn()`, `turnOff()`), and indicators.
   - **`HeatingSystem`:** Inherits from `ControlledSystem` and integrates `TempRampTracker`.
   - **`EvaporatingSystem`:** Inherits from `ControlledSystem` to regulate humidity.

3. **Orchestration (`examples/EggIncubator/EggIncubator.ino`):**
   - Uses `millis()` for non-blocking execution so the microcontroller can handle concurrent tasks (Wi-Fi, displays, buttons).

---

## 🚀 Getting Started

1. Open Arduino IDE.
2. Install required libraries:
   - **DallasTemperature** (by Miles Burton)
   - **OneWire** (by Jim Studt)
   - **DHT sensor library** (by Adafruit)
3. Open `examples/EggIncubator/EggIncubator.ino`.
4. Upload to your ESP32 / Arduino microcontroller.

---

## 🧪 Running Tests

To run the standalone unit test suite:
```bash
g++ -o test_incubator tests/test_incubator.cpp && ./test_incubator
```
