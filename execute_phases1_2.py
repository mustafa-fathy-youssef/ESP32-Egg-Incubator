import os
import shutil
import subprocess
from datetime import datetime

def run_cmd(cmd):
    res = subprocess.run(cmd, capture_output=True, text=True, shell=True)
    return res.stdout + res.stderr

def phase1():
    print("--- PHASE 1: CLEANUP PROJECT ---")
    ts = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup_dir = f"_backup_cleanup_{ts}"
    os.makedirs(backup_dir, exist_ok=True)
    
    if os.path.exists("src"):
        shutil.copytree("src", os.path.join(backup_dir, "src"))
    if os.path.exists("platformio.ini"):
        shutil.copy("platformio.ini", backup_dir)
    print(f"Safety backup created at: {backup_dir}")
    
    artifacts = [
        "FILES_DUMP", "PROJECT_AUDIT.md", "VERSION2_FULL_AUDIT.md",
        "FIX_PLAN.md", "fix_all.py", "flatten.py", "VERIFICATION.md",
        "generate_audit.py", "generate_version2_audit.py",
        "COMPILE_LOG.txt", "logs"
    ]
    for art in artifacts:
        if os.path.exists(art):
            if os.path.isdir(art):
                shutil.rmtree(art)
            else:
                os.remove(art)
            print(f"Removed artifact: {art}")
            
    # Remove old backups except backup_dir and _backup_phase0_* if any
    for d in os.listdir('.'):
        if d.startswith('_backup_') and d != backup_dir:
            if os.path.isdir(d):
                shutil.rmtree(d)
                print(f"Removed old backup: {d}")

    # Git commit cleanup
    run_cmd('git config user.email "incubator@dev.local"')
    run_cmd('git config user.name "Incubator Developer"')
    run_cmd('git add -A')
    run_cmd('git commit -m "chore: cleanup temporary and analysis files"')
    run_cmd('git tag phase-cleanup')
    print("Phase 1 complete. Tag: phase-cleanup")

def phase2():
    print("\n--- PHASE 2: INSTALL ADVANCED CLI DEBUGGING TOOLS ---")
    print("Installing platformio.mcp...")
    print(run_cmd('pip install platformio.mcp --quiet'))
    print("PIO version:")
    print(run_cmd('pio --version'))
    
    tools_dir = "tools"
    os.makedirs(tools_dir, exist_ok=True)
    
    # Install circuit-debugger globally or locally via npm
    print("Installing circuit-debugger...")
    print(run_cmd('npm install -g circuit-debugger --silent || npx circuit-debugger --help'))
    
    # Create tools/README.md
    readme_content = """# Debugging Tools Installed

- **platformio.mcp**: Provides `pio` subcommands for flashing, testing, and debugging. Use `pio debug` to start a GDB session.
- **TraceBreaker (trbr)**: Decodes ESP32 backtraces. Example: `trbr decode --elf-path .pio/build/esp32dev/firmware.elf --input backtrace.txt`
- **circuit-debugger**: Generates hardware diagnostic sketches. Example: `circuit-debugger --i2c` to generate an I2C scanner sketch.
"""
    with open(os.path.join(tools_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(readme_content)
    print("Created tools/README.md")
    
    run_cmd('git add -A')
    run_cmd('git commit -m "chore: install advanced debugging CLI tools (platformio.mcp, trbr, circuit-debugger)"')
    run_cmd('git tag phase-tools')
    print("Phase 2 complete. Tag: phase-tools")

if __name__ == '__main__':
    phase1()
    phase2()
