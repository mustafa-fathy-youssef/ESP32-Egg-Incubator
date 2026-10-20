# Debugging Tools Installed

- **platformio.mcp**: Provides `pio` subcommands for flashing, testing, and debugging. Use `pio debug` to start a GDB session.
- **TraceBreaker (trbr)**: Decodes ESP32 backtraces. Example: `trbr decode --elf-path .pio/build/esp32dev/firmware.elf --input backtrace.txt`
- **circuit-debugger**: Generates hardware diagnostic sketches. Example: `circuit-debugger --i2c` to generate an I2C scanner sketch.
