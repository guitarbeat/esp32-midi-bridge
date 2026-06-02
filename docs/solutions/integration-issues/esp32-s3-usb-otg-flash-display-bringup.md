---
title: ESP32-S3-USB-OTG MIDI BLE bridge flash and bring-up failures
date: 2026-05-31
category: integration-issues
module: firmware/bridge-s3
problem_type: integration_issue
component: development_workflow
symptoms:
  - "USB Serial/JTAG upload fails around 80–280 KB when /dev/cu.usbmodem* is held open by Serial Monitor or read_serial.py"
  - "After arduino-cli upload, chip stays in download mode (serial shows waiting for download) with blank display until manual RESET or esptool watchdog reset"
  - "Boot log reports quad_psram: PSRAM chip is not connected when FQBN uses PSRAM=enabled on ESP32-S3-USB-OTG (no external PSRAM)"
  - "Blank display or LCD init failure after Board.cpp regressions (GPIO 14 MENU vs display pins, SPI speed, init order)"
  - "Boot log repeats ESP-ROM after display->begin OK — reboot loop from USB host rails enabled too early in Board::begin()"
root_cause: config_error
resolution_type: config_change
severity: high
tags:
  - esp32-s3
  - esp32-s3-usb-otg
  - usb-serial-jtag
  - arduino-cli
  - fqbn
  - psram
  - flash-upload
  - display-init
  - uart-midi
related_components:
  - tooling
---

# ESP32-S3-USB-OTG MIDI BLE bridge: USB Serial/JTAG flash, FQBN, and display bring-up

## Problem

Flashing and running `firmware/bridge-s3` on the official **Espressif ESP32-S3-USB-OTG** board (8 MB flash, **no external PSRAM**) failed in multiple overlapping ways: mid-flash upload aborts, post-flash download mode with a blank display, wrong build target options, and display init regressions in `Board.cpp`.

## Symptoms

- Upload stops at **~9.6% (~82 KB compressed)** with esptool errors such as `Chip stopped responding` or `Serial data stream stopped`.
- `lsof /dev/cu.usbmodem*` shows another process (e.g. `read_serial.py`, Python, Serial Monitor) holding the port.
- After upload without manual reset, serial shows:
  ```
  waiting for download
  ```
- With **`PSRAM=enabled`** on this board:
  ```
  quad_psram: PSRAM chip is not connected
  ```
- Boot log repeats `ESP-ROM` / `[LCD] display->begin OK` then USB disconnect — **reboot loop** from USB host rails enabled too early in `Board::begin()` (fixed: rails deferred to `enableUsbHostPower()`).

## What Didn't Work

- Lower upload baud rates (115200, 460800) while the serial port remained open — still failed at the same byte offset.
- Disabling RTS/DTR via board upload properties — no improvement with port contention.
- esptool `--no-stub`, uncompressed write, `--no-diff-verify` — still failed with port held.
- Treating the issue as USB cable/signal integrity — ruled out once upload completed at 921600 with a free port.
- Generic FQBN `esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc` — wrong variant for the official USB-OTG pin map.
- **`PSRAM=enabled`** for canvas RAM — boot error on hardware without external PSRAM.
- GPIO 14 as “display power” — GPIO 14 is the **MENU button** on this board.
- SPI at 40 MHz (refactor regression) — below verified 80 MHz for this ST7789.
- USB host rail init inside `Board::begin()` immediately after LCD init — caused crash/reboot loop before canvas init.
- UART MIDI on **GPIO 43/44** — those pins are USB Serial/JTAG; breaks native USB after boot.
- DTR/RTS toggles during serial probing — left board stuck in download mode.
- Claiming success without capturing boot log evidence — user correctly pushed back (session history).

## Solution

### 1. Correct FQBN — no PSRAM

Use the board-specific FQBN (verified upload PASS):

```bash
esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc
```

Documented in `BUILD.md` and `.github/workflows/arduino-build.yml`.

### 2. Close serial clients before flash

`read_serial.py` warns explicitly:

```python
"""Read ESP32-S3 USB Serial/JTAG logs. Close this before flashing."""
```

Check with `lsof /dev/cu.usbmodem*` before upload.

### 3. Flash and verify helpers

`scripts/flash-bridge-s3.sh` — compile/upload with correct FQBN, port-busy check, post-flash `esptool run`:

```bash
./scripts/flash-bridge-s3.sh
./scripts/verify-boot.sh          # capture boot log + check markers
./scripts/verify-boot.sh --flash  # flash then verify
```

Manual fallback: press **RESET once** (do not hold BOOT).

`read_serial.py --reset` runs watchdog reset then streams logs; detects download mode and prints recovery hints.

### 4. Board.cpp — display init for ESP32-S3-USB-OTG

Key changes in `firmware/bridge-s3/Board.cpp`:

- **GPIO 5 LOW** — LCD enable (active-low, shared with SPI CS)
- **Display init at 80 MHz** in `Board::begin()` only (LCD + buttons + backlight)
- **Backlight GPIO 9 HIGH** after successful `display->begin()`
- **GPIO 14** used only as MENU button input — not display power
- **USB host rails** moved to `Board::enableUsbHostPower()` (500 ms stabilization delay), called from `USBConnection::begin()` after display and canvas are up — **not** in `Board::begin()`

### 5. UartConnection — move off USB Serial/JTAG pins

Moved from GPIO 43/44 to **47 (TX) / 48 (RX)** with runtime guard in `UartConnection.cpp`. Wired in `bridge-s3.ino`. UART MIDI disabled by default (`uartEnabled_ = false` in `BridgeSettings.h`).

### 6. bridge-s3.ino — canvas and USB host startup

- Static canvas allocation at global init (~115 KB while heap is clean)
- `canvas->begin(GFX_SKIP_OUTPUT_BEGIN)` — panel already initialized in `board->begin()`
- `usbMidi.begin(board)` starts USB host after canvas; host rails via `board->enableUsbHostPower()`
- Null-guarded canvas in `loop()`; initial UI draw + `canvas->flush()` on boot

## Why This Works

1. **Exclusive port access**: USB Serial/JTAG is effectively single-client on macOS. A background reader intercepts the same `/dev/cu.usbmodem*` device esptool needs, causing mid-flash disconnects at a consistent byte offset — not random signal noise.

2. **Correct board variant**: `esp32s3usbotg` selects Espressif’s official pin map (ST7789 on SPI, GPIO 5 enable, GPIO 9 backlight, GPIO 8 reset). Generic `esp32s3` options do not guarantee this wiring.

3. **No PSRAM on this hardware**: Enabling PSRAM triggers IDF init that fails at boot. The 240×240 canvas must fit in internal SRAM via early static allocation.

4. **Init order matters**: LCD and canvas must finish before USB host power rails (`enableUsbHostPower`). Enabling GPIO 12/13/17/18 inside `Board::begin()` caused a reboot loop right after `[LCD] display->begin OK`.

5. **GPIO 43/44 are not general-purpose UART**: They are wired to the USB Serial/JTAG controller. Reassigning them kills the native USB debug port.

6. **Post-flash reset**: Arduino’s default `--after hard_reset` via RTS often leaves ESP32-S3 USB Serial/JTAG in download mode. Watchdog reset or physical RESET runs the flashed application.

## Prevention

### Before every flash

1. Close all serial clients: `lsof /dev/cu.usbmodem*`
2. Use the correct FQBN (no PSRAM): `esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc`
3. Prefer `./scripts/flash-bridge-s3.sh` then `./scripts/verify-boot.sh`
4. If display stays blank after flash: press **RESET once** (do not hold BOOT)
5. Stream logs: `python3 read_serial.py --reset`

### After flash — verification checklist

Do not declare success until these are confirmed:

- [ ] Serial shows `[SYSTEM] Native USB CDC Serial connected. Booting...`
- [ ] Serial shows `[LCD] display->begin OK`
- [ ] Serial shows `[SYSTEM] Display canvas initialized.`
- [ ] Serial does **not** show `waiting for download` or `quad_psram`
- [ ] Display shows UI (PIANO BRIDGE header, Bongo Cat)
- [ ] Run `./scripts/verify-boot.sh` on hardware after changes

### Pin / config rules

| Rule | Reason |
|------|--------|
| Never use GPIO 43/44 for UART MIDI | USB Serial/JTAG pins |
| Never use `PSRAM=enabled` on ESP32-S3-USB-OTG | No external PSRAM — boot failure |
| Never drive GPIO 14 as display power | It is the MENU button |
| Never enable USB host rails in `Board::begin()` | Defer to `enableUsbHostPower()` after canvas init |

### Operational quick reference

See `BUILD.md` for recovery steps (BOOT+RESET manual download mode, lower upload speed, UART fallback port).

## Related Issues

- `BUILD.md` — operational build/flash/recovery reference (source material for this doc)
- `scripts/flash-bridge-s3.sh` — compile, upload, watchdog reset helper
- `scripts/verify-boot.sh` — automated boot log verification
- `scripts/wifi_log.py` — Wi-Fi UDP debug receiver (port 3333)
- `read_serial.py` — serial log capture (close before flash)
- `firmware/bridge-s3/README.md` — aligned with `BUILD.md` FQBN/flash workflow

## Status at documentation time

Upload and boot verification tooling is in place (`flash-bridge-s3.sh`, `verify-boot.sh`, `read_serial.py --reset`). Firmware defers USB host rails until after display/canvas init. USB host MIDI starts via `usbMidi.begin(board)`. Wi-Fi UDP debug (`ENABLE_WIFI_DEBUG`, `scripts/wifi_log.py`) supports runtime logs when CDC drops after host mux switch. Run `./scripts/verify-boot.sh` after flash to confirm `[LCD] display->begin OK` and `[SYSTEM] Display canvas initialized.`
