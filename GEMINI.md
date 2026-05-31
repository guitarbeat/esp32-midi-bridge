# Piano BLE Bridge: Agent Instructions

You are an expert embedded software engineer working on the Piano BLE Bridge. This project transforms USB MIDI digital pianos into Bluetooth LE MIDI instruments using ESP32-S3 hardware.

## Project Context

- **Main Firmware**: `firmware/bridge-s3` (ESP32-S3 with native USB-OTG).
- **Fallback Firmware**: `bridge-classic` (Classic ESP32 + MAX3421E).
- **Hardware**: Espressif ESP32-S3-USB-OTG is the primary target (8 MB flash, no PSRAM).
- **Framework**: Arduino-ESP32 (Core 3.3.x).

## Architectural Principles

We follow the **Deep Module** philosophy (from John Ousterhout's *A Philosophy of Software Design*):
- **Modules** should have simple interfaces that hide significant complexity (**Deep**).
- Avoid **Shallow** modules where the interface is as complex as the implementation.
- **Seams** are where interfaces live; use them to decouple components (e.g., UI, MIDI Processing, Connectivity).
- **Locality**: Keep related logic and state together within a module.
- **Leverage**: Modules should provide high value to their callers.

## Agent-Native Conventions

- **Source of Truth**: `./README.md` for user-facing info, `./BUILD.md` for build steps.
- **Domain Language**: See `./CONTEXT.md` for the shared vocabulary (e.g., Module, Seam, Adapter).
- **Refactoring**: When improving code, aim to "deepen" modules. Move logic from `bridge-s3.ino` into dedicated modules (e.g., `BridgeUi`, `ConnectivityManager`).
- **Instructions**: Each major subdirectory has its own `GEMINI.md` with scoped instructions.

## Build, Flash & Verify

- **FQBN**: `esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc` — never `PSRAM=enabled`.
- **Flash**: `./scripts/flash-bridge-s3.sh` (close `read_serial.py` first).
- **Verify boot**: `./scripts/verify-boot.sh` — requires `[LCD] display->begin OK` and no `waiting for download`.
- **Serial logs**: `python3 read_serial.py --reset`
- **Wi-Fi logs** (when host mode kills CDC): compile with `-DENABLE_WIFI_DEBUG=1`, run `python3 scripts/wifi_log.py`.
- **Unit tests**: `./scripts/test.sh`

## Documentation

- `AGENTS.md` — learned user preferences and durable workspace facts (flash, FQBN, init order).
- `docs/solutions/` — documented fixes (e.g. [ESP32-S3 flash/display bring-up](docs/solutions/integration-issues/esp32-s3-usb-otg-flash-display-bringup.md)).
- `docs/superpowers/specs/` — design specs (e.g. [full-stack milestone](docs/superpowers/specs/2026-05-31-bridge-full-stack-milestone-design.md)).
- Use **ADRs** in `docs/adr/` for significant structural changes.
- Keep `CONTEXT.md` updated as the domain language evolves.

## Verification discipline

Do not claim flash/display/USB fixes work without runtime evidence: successful upload, boot log markers, and (when applicable) working display or MIDI path.
