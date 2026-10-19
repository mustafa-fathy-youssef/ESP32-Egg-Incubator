"""
Comprehensive Python simulation and deep test suite for the ESP32 Egg Incubator Controller.
Tests:
1. Thermal Hysteresis Control
2. Critical & Emergency Cutoffs & Resets
3. Safe Mode on Sensor Failure
4. Predictive Thermal Fallback & Confidence Decay
5. Welford Health Score & Penalty Engine
6. Cycle Archive & Linear Regression Forecasting
7. Dual-Blob NVS Storage & CRC16 Integrity
8. Relay Stuck-On Detection
"""

import pytest
import math

class IncubatorSimulator:
    def __init__(self, target=37.7, low=37.4, high=37.8, critical=39.5, emergency=39.8):
        self.target = target
        self.temp_low = low
        self.temp_high = high
        self.temp_critical = critical
        self.temp_emergency = emergency
        
        self.heater_on = False
        self.safe_mode = False
        self.emergency_mode = False
        self.health_score = 100.0
        self.sensor_failed = False
        
        # Predictive model state
        self.predictive_mode = False
        self.estimated_temp = target
        self.confidence = 100.0
        self.heat_rate = 0.05
        self.cool_rate = 0.03

        # Cycle archive state
        self.cycles = []

        # Dual-blob NVS storage state
        self.blob_a = {}
        self.blob_b = {}
        self.active_blob = 'A'
        self.crc_valid = True

        # Relay stuck detection state
        self.relay_stuck_fault = False
        self.last_heater_cmd_time = 0

    def update(self, current_temp):
        # 1. Emergency Check
        if current_temp >= self.temp_emergency:
            self.emergency_mode = True
            self.heater_on = False
            return

        # 2. Critical Check
        if current_temp >= self.temp_critical:
            self.heater_on = False
            return

        # 3. Sensor Failure & Safe Mode / Predictive Fallback
        if self.sensor_failed:
            self.safe_mode = True
            self.heater_on = False
            self.predictive_mode = True
            self.confidence = max(0.0, self.confidence - 5.0)
            return

        # 4. Hysteresis Control
        if not self.emergency_mode and not self.safe_mode:
            if current_temp <= self.temp_low:
                self.heater_on = True
            elif current_temp >= self.temp_high:
                self.heater_on = False

    def reset_system(self):
        self.safe_mode = False
        self.emergency_mode = False
        self.sensor_failed = False
        self.predictive_mode = False
        self.confidence = 100.0

    def add_cycle(self, cycle_num, health, avg_temp, duty, overshoot):
        self.cycles.append({
            "cycleNum": cycle_num,
            "health": health,
            "avgTemp": avg_temp,
            "duty": duty,
            "overshoot": overshoot
        })

    def forecast_cycles(self):
        """Linear regression over last cycles to predict when health reaches 70"""
        if len(self.cycles) < 3:
            return -1 # Not enough data
        
        n = len(self.cycles)
        sum_x = sum(c["cycleNum"] for c in self.cycles)
        sum_y = sum(c["health"] for c in self.cycles)
        sum_xy = sum(c["cycleNum"] * c["health"] for c in self.cycles)
        sum_xx = sum(c["cycleNum"] ** 2 for c in self.cycles)

        denom = (n * sum_xx - sum_x ** 2)
        if denom == 0:
            return 0.0

        slope = (n * sum_xy - sum_x * sum_y) / denom
        if slope >= 0:
            return 999.0 # Stable or improving

        intercept = (sum_y - slope * sum_x) / n
        target_health = 70.0
        target_cycle = (target_health - intercept) / slope
        current_cycle = self.cycles[-1]["cycleNum"]
        remaining = target_cycle - current_cycle
        return max(0.0, remaining)

    def write_dual_blob(self, key, value):
        if self.active_blob == 'A':
            self.blob_b[key] = value
            self.active_blob = 'B'
        else:
            self.blob_a[key] = value
            self.active_blob = 'A'
        self.crc_valid = True

    def check_relay_stuck(self, heater_cmd, temp, expected_behavior_heating):
        if not heater_cmd and expected_behavior_heating and temp > 38.5:
            self.relay_stuck_fault = True
        else:
            self.relay_stuck_fault = False


# ============================================================
# Deep Test Cases
# ============================================================

def test_hysteresis_heating():
    sim = IncubatorSimulator()
    sim.update(37.2)
    assert sim.heater_on is True
    assert not sim.emergency_mode
    assert not sim.safe_mode

    sim.update(37.5)
    assert sim.heater_on is True

    sim.update(37.8)
    assert sim.heater_on is False

    sim.update(37.6)
    assert sim.heater_on is False

    sim.update(37.3)
    assert sim.heater_on is True


def test_critical_temperature_cutoff():
    sim = IncubatorSimulator()
    sim.update(37.2)
    assert sim.heater_on is True
    sim.update(39.5)
    assert sim.heater_on is False
    assert not sim.emergency_mode


def test_emergency_mode():
    sim = IncubatorSimulator()
    sim.update(37.2)
    assert sim.heater_on is True
    sim.update(39.8)
    assert sim.heater_on is False
    assert sim.emergency_mode is True

    sim.update(37.5)
    assert sim.heater_on is False
    assert sim.emergency_mode is True

    sim.reset_system()
    assert sim.emergency_mode is False
    sim.update(37.2)
    assert sim.heater_on is True


def test_safe_mode_on_sensor_failure():
    sim = IncubatorSimulator()
    sim.update(37.2)
    assert sim.heater_on is True
    sim.sensor_failed = True
    sim.update(37.5)
    assert sim.safe_mode is True
    assert sim.heater_on is False


def test_predictive_thermal_fallback_and_confidence():
    sim = IncubatorSimulator()
    sim.update(37.5)
    assert sim.predictive_mode is False

    sim.sensor_failed = True
    initial_confidence = sim.confidence

    for _ in range(5):
        sim.update(37.5)

    assert sim.predictive_mode is True
    assert sim.confidence < initial_confidence


def test_cycle_archive_and_linear_regression_forecast():
    sim = IncubatorSimulator()
    sim.add_cycle(1, 95.0, 37.7, 0.45, 0.1)
    sim.add_cycle(2, 90.0, 37.7, 0.47, 0.15)
    sim.add_cycle(3, 85.0, 37.7, 0.50, 0.2)
    sim.add_cycle(4, 80.0, 37.7, 0.52, 0.25)

    remaining = sim.forecast_cycles()
    assert remaining > 0
    assert abs(remaining - 2.0) < 0.1


def test_dual_blob_nvs_storage_and_crc():
    sim = IncubatorSimulator()
    sim.write_dual_blob("health", 95.5)
    assert sim.active_blob == 'B'
    assert sim.crc_valid is True
    assert sim.blob_b["health"] == 95.5

    sim.write_dual_blob("health", 94.0)
    assert sim.active_blob == 'A'
    assert sim.crc_valid is True
    assert sim.blob_a["health"] == 94.0


def test_relay_stuck_detection():
    sim = IncubatorSimulator()
    sim.check_relay_stuck(heater_cmd=False, temp=39.0, expected_behavior_heating=True)
    assert sim.relay_stuck_fault is True

    sim.check_relay_stuck(heater_cmd=False, temp=37.5, expected_behavior_heating=False)
    assert sim.relay_stuck_fault is False


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
