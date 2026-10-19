"""
Interactive Simulation Runner for ESP32 Egg Incubator.
Simulates temperature control, heater hysteresis, safety cutoffs, and health score over time.
"""

import time
from test_incubator import IncubatorSimulator

def run_simulation():
    print("🥚 Starting ESP32 Egg Incubator Simulation...")
    sim = IncubatorSimulator()
    
    # Simulate a temperature profile: start cold (36.0), heat up, overshoot, stabilize, spike to critical/emergency, recover
    profile = [
        36.0, 36.2, 36.5, 37.0, 37.3, 37.5, 37.7, 37.9, 38.0, 37.8,
        37.6, 37.5, 37.3, 37.2, 37.4, 37.6, 37.8, 38.1, 39.0, 39.6,
        40.0, 39.7, 39.4, 38.0, 37.7
    ]

    print(f"{'Step':<5} | {'Temp (°C)':<10} | {'Heater':<8} | {'Safe Mode':<10} | {'Emergency':<10}")
    print("-" * 60)

    for i, temp in enumerate(profile):
        sim.update(temp)
        print(f"{i+1:<5} | {temp:<10.2f} | {'ON' if sim.heater_on else 'OFF':<8} | {str(sim.safe_mode):<10} | {str(sim.emergency_mode):<10}")
        time.sleep(0.05)

    print("-" * 60)
    print("✅ Simulation completed successfully.")

if __name__ == "__main__":
    run_simulation()
