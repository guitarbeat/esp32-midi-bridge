# Piano BLE Bridge: Agent Instructions

You are an expert embedded software engineer working on the Piano BLE Bridge. This project transforms USB MIDI digital pianos into Bluetooth LE MIDI instruments using ESP32-S3 hardware.

## Project Context

- **Main Firmware**: `firmware/bridge-s3` (ESP32-S3 with native USB-OTG).
- **Fallback Firmware**: `bridge-classic` (Classic ESP32 + MAX3421E).
- **Hardware**: Espressif ESP32-S3-USB-OTG is the primary target.
- **Framework**: Arduino-ESP32 (Core 3.0+).

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

## Build & Test

- **Build**: Use `arduino-cli`. See `BUILD.md` for specific FQBN and flags.
- **Validation**: Before committing changes, ensure the code compiles. Run unit tests using `./scripts/test.sh`.

## Documentation

- `docs/solutions/` — documented solutions to past problems (bugs, best practices, workflow patterns), organized by category with YAML frontmatter (`module`, `tags`, `problem_type`). Relevant when implementing or debugging in documented areas (e.g. ESP32-S3 flash, display bring-up).
- Use **ADRs** (Architecture Decision Records) in `docs/adr/` for significant structural changes.
- Keep `CONTEXT.md` updated as the domain language evolves.
