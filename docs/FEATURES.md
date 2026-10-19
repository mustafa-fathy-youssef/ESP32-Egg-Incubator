# ESP32 Egg Incubator Controller v2.0.1 — Complete Features Guide

This document provides an exhaustive explanation of all features, subsystems, algorithms, safety mechanisms, and command interfaces implemented in the **ESP32 Egg Incubator Controller (v2.0.1)**.

---

## 1. Core Thermal Control & Hysteresis Engine

The incubator maintains a precise biological temperature environment required for poultry egg incubation (**37.7 °C** target).

- **Hysteresis Band Control:**
  - **Target Temperature:** `37.7 °C`
  - **Low Threshold (`TEMP_LOW`):** `37.4 °C` (Heater turns **ON**)
  - **High Threshold (`TEMP_HIGH`):** `37.8 °C` (Heater turns **OFF**)
  - **Exponential Moving Average (EMA) Smoothing:** Raw sensor readings (`alpha = 0.25`) are smoothed to eliminate transient noise and relay chatter without introducing significant lag.

- **Dynamic Temperature Adjustment:**
  - Operators can update the target temperature at runtime using serial commands (`SET_TEMP`), which automatically recalculates low and high hysteresis boundaries.

---

## 2. Multi-Tier Safety & Protection System

Designed with safety-critical reliability engineering, the firmware operates offline-first with multiple independent layers of hardware and software protection:

1. **Critical Temperature Cutoff (`TEMP_CRITICAL = 39.5 °C`):**
   - Instantly deactivates the heater if temperature approaches dangerous levels.
2. **Emergency Mode (`TEMP_EMERGENCY = 39.8 °C`):**
   - Forces immediate shutdown of the heater and egg turner.
   - **Automatic Exit:** Exits automatically once temperature drops below `TEMP_HIGH` (37.8 °C) and remains stable continuously for 10 seconds (manual override commands like `RESET` or `EXITEMG` also supported).
3. **Relay Stuck-On Detection:**
   - Continuously monitors whether the heater relay is commanded OFF while temperature continues to rise (`>= tempHigh + 2.0 °C`).
   - If sustained for 30 seconds, flags a hardware relay failure, enters **Safe Mode**, and triggers alarms.
4. **Sensor Failure Detection & Fail LED (GPIO 5):**
   - Monitors DS18B20 1-Wire bus and DHT22 humidity/temperature sensors.
   - If sensors fail repeatedly (>= 3 consecutive failures), lights the physical **Fail LED on GPIO 5**, displays "Sensor Error!" on the LCD, and transitions into fallback/safe routines.
   - **Automatic Exit:** Exits Safe Mode automatically once sensors recover and relay status returns to normal, remaining stable continuously for 10 seconds (manual override commands like `RESET` or `EXITSAFE` also supported).
5. **Hardware Task Watchdog (WDT):**
   - Configured with an 8-second timeout (`esp_task_wdt`) guarding against infinite loops or task hangups.

---

## 3. Environmental & Actuator Management

The system manages 4 core hardware actuators connected via digital relay outputs:

| Actuator | GPIO Pin | Default Behavior | Description |
|---|---|---|---|
| **Fan** | GPIO 13 | Always ON (Failsafe) | Continuous internal airflow circulation to prevent hot spots. |
| **Heater** | GPIO 27 | Hysteresis Controlled | 230V/12V heating element managed by hysteresis and safety limits (max 30s runtime protection). |
| **Egg Turner** | GPIO 14 | Cyclic Scheduled | Automatically tilts eggs: **60 seconds OFF / 15 seconds ON** duty cycle to prevent embryo adhesion. |
| **Evaporator** | GPIO 12 | Humidity Regulated | Maintains humidity between **45% RH (ON)** and **55% RH (OFF)**, falling back to a timed schedule if humidity sensors fail. |

---

## 4. Health Score & Welford Statistics Engine

- **Real-Time Health Score (0–100):**
  - Evaluates system stability, sensor health, cycle continuity, and fault frequency in real time.
- **Welford's Algorithm:**
  - Computes online running variance and mean of thermal data without requiring large memory buffers.
- **Fault Ring Buffer:**
  - Maintains a 32-slot fault history ring buffer logging timestamps, fault codes, and severity levels.

---

## 5. Predictive Thermal Fallback Model

- **Open-Loop Thermal Estimation:**
  - Operates passively in the background, learning heating and cooling rates during normal operation.
- **Sensor Loss Fallback:**
  - If temperature sensors fail completely, the predictive model takes over to estimate internal core temperature.
  - Confidence decays over time; if confidence drops below safety thresholds or the estimate exceeds safety limits, the system forces Safe Mode.

---

## 6. Dual-Blob NVS Storage with CRC16 Integrity

- **Non-Volatile Storage (NVS):**
  - Persists system configuration, timer state (`elapsed_sec`), cycle count (`cycle_count`), and diagnostic blobs.
- **Dual-Blob Architecture (Block A / Block B) + CRC16:**
  - Alternates writes between two NVS blocks protected by CRC16 checksums. Protects against corruption during power glitches or brownouts.

---

## 7. Cycle Archive & Degradation Forecasting

- **Cycle Archiving:**
  - Automatically records metrics for the last **10 completed incubation cycles** (duration, health score, average temperature, duty cycle, average overshoot).
- **Linear Regression Forecasting (`FORECAST`):**
  - Analyzes historical cycle trends and computes degradation forecasts with approximately 95% confidence intervals to predict hatching performance and hardware aging.

---

## 8. Non-Blocking Cloud & Telegram Integration

- **Offline-First Isolation:**
  - Network operations (WiFi, Adafruit IO, Telegram Bot) run asynchronously in non-blocking background routines. A network outage or slow server will never block or delay the real-time thermal control loop.
- **Exponential-Backoff WiFi Handler:**
  - Reconnects intelligently (from 1 minute up to 1 hour intervals) without freezing the controller.
- **Debounced Telegram Alerts:**
  - Sends asynchronous alert notifications for faults, temperature anomalies, and periodic health summaries.

---

## 9. Complete Serial Command Reference

Connect via Serial Monitor at **115200 baud** and type any of the following commands:

| Command | Description |
|---|---|
| `HELP` / `?` | Display full command list and help reference. |
| `STATUS` | Print current system status (elapsed time, raw/smoothed temp, heater/turner state, safe/emergency mode). |
| `VERSION` | Display firmware version (`2.0.1`), chip model, revision, and CPU frequency. |
| `HEAP` | Show free SRAM heap memory and minimum ever free heap. |
| `REPORT` | Print full diagnostic report (uptime, boot count, sensor status, humidity fails). |
| `HEALTH` | Print system health score summary and degradation breakdown. |
| `DIAG` | Print diagnostic status and NVS storage block stats. |
| `FAULTS` | Print the 32-slot fault ring buffer history. |
| `FORECAST` | Run linear regression degradation forecast on the last 10 cycle archives. |
| `RESET` | Perform manual reset of timer, clear active safe/emergency locks. |
| `EXITSAFE` | Clear Safe Mode manually. |
| `EXITEMG` | Clear Emergency Mode manually. |
| `RESETDHT` | Re-initialize and reset all humidity (DHT22) sensors. |
| `MONITORON` | Enable periodic automatic Telegram monitoring (every 5 minutes). |
| `MONITOROFF` | Disable periodic automatic Telegram monitoring. |
| `TESTTG` | Send a test Telegram alert notification with a random fault code. |
| `CLEARDIAG` | Initiate diagnostics data erasure (requires typing `CONFIRM` within 5 seconds). |
| `SET_TEMP <val>` | Set target temperature (range: 30.0 °C – 42.0 °C). |
| `SET_CRITICAL <val>` | Set critical temperature threshold (range: 38.0 °C – 45.0 °C). |
| `SET_EMERGENCY <val>` | Set emergency temperature threshold (range: 39.0 °C – 48.0 °C). |
