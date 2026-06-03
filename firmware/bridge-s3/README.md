# Bridge S3: USB MIDI to BLE MIDI Bridge

This sketch turns an ESP32-S3 board with native USB-OTG into a one-way MIDI bridge:

```text
USB MIDI piano/controller -> ESP32-S3 -> BLE MIDI -> phone/tablet/computer
```

It is intentionally guarded for ESP32-S3 only. Classic ESP32 boards with
ESP32-WROOM-32 modules cannot host a USB MIDI keyboard through firmware alone.

## Board Check

- `ESP32-WROOM-32`, `ESP32-2432S024`, or `ESP32-2432S028`: classic ESP32. This sketch will not compile for it because the chip has no native USB host peripheral.
- `ESP32-S3`, `ESP-WROOM-S3`, or `ESP32-S3-2432...`: likely usable if the board exposes the USB-OTG D+/D- lines and can power the keyboard.
- Classic ESP32 fallback requires an external USB host controller, for example a MAX3421E module. That needs a separate driver path and is not this firmware.

## Hardware

- ESP32-S3 board with USB-OTG host support.
- USB OTG adapter/cable for the MIDI keyboard.
- A class-compliant USB MIDI keyboard or controller.
- Optional separate USB cable/port for flashing and serial logs, depending on the board.

The official Espressif **ESP32-S3-USB-OTG** development board is the primary target (8 MB flash, **no external PSRAM**). Its display shows firmware status and the Bluetooth MIDI name at boot.

## Build and flash (ESP32-S3-USB-OTG)

Use the board-specific FQBN from [docs/build.md](../../docs/build.md):

```bash
./scripts/flash-bridge-s3.sh
./scripts/verify-boot.sh          # capture boot log + check LCD markers
./scripts/verify-boot.sh --flash  # flash then verify
python3 read_serial.py --reset    # watchdog reset, then stream logs
python3 read_serial.py            # close before flashing
```

Do **not** enable PSRAM on the ESP32-S3-USB-OTG board.

**Init order:** `Board::begin()` brings up the LCD only. USB host power rails (GPIO 12/13/17/18) run later in `Board::enableUsbHostPower()`, called from `UsbMidiHost::begin()` after the canvas is ready. Enabling host rails inside `Board::begin()` caused a reboot loop right after `[LCD] display->begin OK`.

When USB host mode is active, native USB CDC may stop. Enable Wi-Fi debug logging (see [docs/build.md](../../docs/build.md)) and run `python3 scripts/wifi_log.py`.

Design reference: [full-stack milestone design](../../docs/superpowers/specs/2026-05-31-bridge-full-stack-milestone-design.md).

Agent instructions for this sketch: [agent instructions](../../docs/agent-instructions.md#firmware-bridge-s3).

## Test

1. Flash with `./scripts/flash-bridge-s3.sh` and confirm `./scripts/verify-boot.sh` passes.
2. Connect the keyboard to the Type-A **host** port; power **USB_DEV** for VBUS if needed.
3. Open a BLE MIDI app and connect to `Piano BLE Bridge`.
4. Press keys and confirm the app receives notes.
5. Optional: `python3 scripts/wifi_log.py` when `ENABLE_WIFI_DEBUG=1` is set at compile time.

The serial or Wi-Fi log prints USB/BLE connection state and basic MIDI event details.
