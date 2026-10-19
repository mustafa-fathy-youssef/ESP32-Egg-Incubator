"""
Unified Task Runner for ESP32 Egg Incubator.
Provides simple command-line interface for testing, simulation, and code quality checks
designed for integration with Gemini CLI and developer workflows.
"""

import subprocess
import sys


def run_cmd(cmd):
    print(f"▶ Running: {cmd}")
    result = subprocess.run(cmd, shell=True, check=False)
    if result.returncode != 0:
        print(f"❌ Command failed with exit code {result.returncode}")
        sys.exit(result.returncode)

def cmd_test():
    """Run pytest simulation and deep test suite"""
    run_cmd("python -m pytest test_incubator.py -v")

def cmd_simulate():
    """Run thermal profile and sensor test simulation"""
    run_cmd("python simulate_incubator.py")

def cmd_lint():
    """Run ruff linter on python files"""
    run_cmd("python -m ruff check .")

def cmd_all():
    """Run all tests, simulations, and linters"""
    print("🚀 Running full validation pipeline...")
    cmd_lint()
    cmd_test()
    cmd_simulate()
    print("✨ All validation tasks passed successfully!")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python tasks.py [test|simulate|lint|all]")
        sys.exit(1)
        
    action = sys.argv[1].lower()
    if action == "test":
        cmd_test()
    elif action == "simulate":
        cmd_simulate()
    elif action == "lint":
        cmd_lint()
    elif action == "all":
        cmd_all()
    else:
        print(f"Unknown task: {action}")
        print("Available tasks: test, simulate, lint, all")
        sys.exit(1)
