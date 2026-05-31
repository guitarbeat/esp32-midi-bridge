# bridge-s3: Specific Instructions

This directory contains the primary firmware for ESP32-S3 boards.

## Structure

- `bridge-s3.ino`: Shallow coordinator — delegates to deep modules.
- `Board.cpp/h`: Display, buttons, battery; `enableUsbHostPower()` for USB-OTG rails.
- `BridgeUi.cpp/h`: Display rendering and visual feedback.
- `USBConnection.cpp/h`: USB MIDI host stack (dedicated core-0 task, dual IN pipes, queued OUT).
- `BLEConnection.cpp/h`: Bluetooth LE MIDI peripheral.
- `MidiBridge.cpp/h`: Transport hub — routes MIDI between USB, BLE, RTP, UART.
- `WifiDebugLog.cpp/h`: Optional UDP debug logging when `ENABLE_WIFI_DEBUG=1`.
- `BridgeLog.h`: `BRIDGE_LOG` / `BRIDGE_LOG_LN` — Serial + optional Wi-Fi mirror.
- `animation/`: Bongo Cat animation engine.

## Build target

ESP32-S3-USB-OTG (8 MB flash, **no PSRAM**):

```text
esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc
```

Flash: `./scripts/flash-bridge-s3.sh` from repo root. Verify: `./scripts/verify-boot.sh`.

## USB host notes

- `usbMidi.begin(board)` installs USB host, then enables host power rails (GPIO 18/12/17/13).
- After host mux switches, native USB CDC may stop — use `ENABLE_WIFI_DEBUG=1` + `scripts/wifi_log.py`.
- USB stack includes Roland vendor-mode warning, Casio connect delay, endpoint 0x81 fallback.
- Patterns adapted from [esp32-usb-host-midi-library](https://github.com/enudenki/esp32-usb-host-midi-library) (Omocha, MIT).

## Guidelines

- **Memory**: 240×240 canvas @ 16bpp ≈ 115 KB — allocated statically at boot; no PSRAM on USB-OTG board.
- **Logging**: Prefer `BRIDGE_LOG` in USB/BLE/SYSTEM paths so Wi-Fi debug receives them when enabled.
- **GPIO 43/44**: USB Serial/JTAG only — never UART MIDI. Default UART pins: 47 TX / 48 RX.

## Docs

- [BUILD.md](../../BUILD.md) — FQBN, scripts, Wi-Fi debug, recovery
- [README.md](README.md) — sketch-specific quick start
- [Full-stack milestone design](../../docs/superpowers/specs/2026-05-31-bridge-full-stack-milestone-design.md)
