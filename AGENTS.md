# Piano BLE Bridge — Agent Instructions

You are an expert embedded software engineer working on the Piano BLE Bridge. This project transforms USB MIDI digital pianos into Bluetooth LE MIDI instruments using ESP32-S3 hardware.

## Sources of truth

| File | Purpose |
|------|---------|
| [README.md](README.md) | User-facing project overview, hardware, features |
| [BUILD.md](BUILD.md) | Build, flash, verify, Wi-Fi debug, OTA |
| [CONTEXT.md](CONTEXT.md) | Shared vocabulary — domain terms and architecture language |
| [.agents/README.md](.agents/README.md) | Index of project-specific agent skills |

## Project context

- **Main firmware**: [firmware/bridge-s3](firmware/bridge-s3) (ESP32-S3 with native USB-OTG).
- **Fallback firmware**: [firmware/bridge-classic](firmware/bridge-classic) (Classic ESP32 + MAX3421E).
- **Hardware**: Espressif ESP32-S3-USB-OTG is the primary target (8 MB flash, no PSRAM).
- **Framework**: Arduino-ESP32 (Core 3.3.x).

## Architectural principles

We follow the **Deep Module** philosophy (John Ousterhout, *A Philosophy of Software Design*):

- **Modules** should have simple interfaces that hide significant complexity (**deep**).
- Avoid **shallow** modules where the interface is as complex as the implementation.
- **Seams** are where interfaces live; use them to decouple components (UI, MIDI processing, connectivity).
- **Locality**: keep related logic and state together within a module.
- **Leverage**: modules should provide high value to their callers.

Use the terms in [CONTEXT.md](CONTEXT.md) exactly when discussing or implementing changes.

## Build, flash & verify

- **FQBN**: `esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc` — never `PSRAM=enabled`.
- **Flash**: `./scripts/flash-bridge-s3.sh` (close `read_serial.py` first).
- **Verify boot**: `./scripts/verify-boot.sh` — requires `[LCD] display->begin OK` and no `waiting for download`.
- **Serial logs**: `python3 read_serial.py --reset`
- **Wi-Fi logs** (when host mode kills CDC): compile with `-DENABLE_WIFI_DEBUG=1`, run `python3 scripts/wifi_log.py`.
- **Unit tests**: `./scripts/test.sh`

## Firmware: bridge-s3

Primary sketch for ESP32-S3 boards with USB-OTG host.

### Structure

- `bridge-s3.ino`: Shallow coordinator — delegates to deep modules.
- `Board.cpp/h`: Display, buttons, battery; `enableUsbHostPower()` for USB-OTG rails.
- `BridgeUi.cpp/h`: Display rendering and visual feedback.
- `USBConnection.cpp/h`: USB MIDI host stack (dedicated core-0 task, dual IN pipes, queued OUT).
- `BLEConnection.cpp/h`: Bluetooth LE MIDI peripheral.
- `MidiBridge.cpp/h`: Transport hub — routes MIDI between USB, BLE, RTP, UART.
- `WifiDebugLog.cpp/h`: Optional UDP debug logging when `ENABLE_WIFI_DEBUG=1`.
- `BridgeLog.h`: `BRIDGE_LOG` / `BRIDGE_LOG_LN` — Serial + optional Wi-Fi mirror.
- `animation/`: Bongo Cat animation engine.

### USB host notes

- `usbMidi.begin(board)` installs USB host, then enables host power rails (GPIO 18/12/17/13).
- After host mux switches, native USB CDC may stop — use `ENABLE_WIFI_DEBUG=1` + `scripts/wifi_log.py`.
- USB stack includes Roland vendor-mode warning, Casio connect delay, endpoint 0x81 fallback.
- Patterns adapted from [esp32-usb-host-midi-library](https://github.com/enudenki/esp32-usb-host-midi-library) (Omocha, MIT).

### Guidelines

- **Memory**: 240×240 canvas @ 16bpp ≈ 115 KB — allocated statically at boot; no PSRAM on USB-OTG board.
- **Logging**: Prefer `BRIDGE_LOG` in USB/BLE/SYSTEM paths so Wi-Fi debug receives them when enabled.
- **GPIO 43/44**: USB Serial/JTAG only — never UART MIDI. Default UART pins: 47 TX / 48 RX.
- **Init order**: `Board::begin()` brings up the LCD only. USB host power rails run later in `Board::enableUsbHostPower()`, called from `USBConnection::begin()`.

## Documentation

- `docs/solutions/` — documented fixes (e.g. [ESP32-S3 flash/display bring-up](docs/solutions/integration-issues/esp32-s3-usb-otg-flash-display-bringup.md)).
- `docs/superpowers/specs/` — design specs (e.g. [full-stack milestone](docs/superpowers/specs/2026-05-31-bridge-full-stack-milestone-design.md)).
- Use **ADRs** in `docs/adr/` for significant structural changes.
- Keep [CONTEXT.md](CONTEXT.md) updated as the domain language evolves.

## Verification discipline

Do not claim flash/display/USB fixes work without runtime evidence: successful upload, boot log markers, and (when applicable) working display or MIDI path.

## Learned user preferences

- Verify with runtime evidence (successful flash, serial boot log, display output) before claiming something is fixed or working.
- Prefer simple, user-friendly, buildable, robust open firmware that anyone can flash for USB MIDI to Bluetooth LE MIDI.
- README should explain project motivation as an open replacement for commercial USB-to-BLE MIDI dongles (e.g. DoReMiDi UTB-21 Pro).

## Learned workspace facts

- Primary hardware target is the Espressif ESP32-S3-USB-OTG board (8 MB flash, no external PSRAM).
- Required FQBN: `esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc` — never `PSRAM=enabled`.
- Helper scripts: `flash-bridge-s3.sh`, `verify-boot.sh`, `read_serial.py` (`--reset`), `wifi_log.py` (port 3333).
- Flash uploads fail around 80–280 KB when another process holds `/dev/cu.usbmodem*`; close serial tools before flashing.
- After upload the chip may stay in download mode; press RESET once or use `verify-boot.sh` / `read_serial.py --reset`.
- GPIO 43/44 are USB Serial/JTAG — must not be used for UART MIDI (default: 47 TX / 48 RX).
- Display init in `Board::begin()` runs before USB host; host rails enabled in `USBConnection::begin()` via `Board::enableUsbHostPower()`.
- Healthy boot logs: `[LCD] display->begin OK`, `[SYSTEM] Display canvas initialized`.
- After USB host mux (GPIO 18 HIGH), CDC may stop — use `ENABLE_WIFI_DEBUG=1` and `BRIDGE_LOG` + `wifi_log.py`.
- USB host VBUS for keyboard needs 5 V on Type-A port via **USB_DEV**; board power alone may not enumerate the piano.
- LCD enable GPIO 5 (LOW); backlight GPIO 9.
- USB stack credits: Saulo Veríssimo, touchgadget, Omocha (enudenki) — patterns in `USBConnection`, not a library dependency.
