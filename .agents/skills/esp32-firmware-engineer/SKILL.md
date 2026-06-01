---
name: esp32-firmware-engineer
description: ESP32 embedded firmware engineering for this project — hardware truth, build verification, display/peripheral correctness, and observability. Use when working on Board, USB host, BLE, display, GPIO, or embedded debugging on ESP32-S3.
---

# ESP32 Firmware Engineer

Embedded firmware skill for the Piano BLE Bridge. Read [AGENTS.md](../../../AGENTS.md) first for project-specific FQBN, pins, init order, and verification discipline.

## This project's stack

- **Target**: ESP32-S3-USB-OTG (8 MB flash, **no PSRAM**).
- **Framework**: Arduino-ESP32 core 3.3.x (not raw ESP-IDF for the main firmware).
- **Build**: `./scripts/flash-bridge-s3.sh`, verify with `./scripts/verify-boot.sh`.
- **Logs**: `python3 read_serial.py --reset`; Wi-Fi mirror via `ENABLE_WIFI_DEBUG=1` + `scripts/wifi_log.py`.

## Non-negotiable values

Read [references/values.md](references/values.md) first. Key rules adapted for this repo:

1. **Hardware truth before code** — confirm variant, pins, power rails, and display controller before changing Board/USB/display code.
2. **Variant certainty** — this repo is ESP32-S3 only for bridge-s3; never enable PSRAM on the USB-OTG board.
3. **Build-proven changes** — compile and flash before claiming a fix works; check boot log markers.
4. **Correct data format first** — display bugs are often pixel format, byte order, or init-sequence mismatches.
5. **Explicit unknowns** — state what was verified on hardware vs. inferred from code review.

## References

| File | Topic |
|------|-------|
| [references/values.md](references/values.md) | Core engineering values and blocking rules |
| [references/microcontroller-programming.md](references/microcontroller-programming.md) | GPIO, interrupts, timers, watchdogs |
| [references/rtos-patterns.md](references/rtos-patterns.md) | FreeRTOS task design, pinning, queues |
| [references/lvgl-display.md](references/lvgl-display.md) | Display stack patterns (this project uses Arduino_GFX canvas, not LVGL) |

## Project-specific hardware facts

See **Learned workspace facts** in [AGENTS.md](../../../AGENTS.md). Critical pins:

- LCD enable GPIO 5 (LOW), backlight GPIO 9.
- USB host mux/power: GPIO 18/12/17/13 via `Board::enableUsbHostPower()`.
- GPIO 43/44: USB Serial/JTAG — never UART MIDI (default UART: 47 TX / 48 RX).
