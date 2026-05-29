# Piano BLE Bridge

Piano BLE Bridge turns a USB MIDI digital piano, keyboard, or controller into a
Bluetooth LE MIDI instrument using inexpensive ESP32 hardware.

```text
USB MIDI piano -> ESP32 bridge -> Bluetooth LE MIDI -> iPad, iPhone, Mac, Android, DAW
```

The main target is an ESP32-S3 board with native USB-OTG host support. The repo
also includes a classic ESP32 fallback sketch for boards that use an external
MAX3421E USB host module.

## What Works

- USB MIDI input from class-compliant digital pianos and MIDI controllers,
  including many Roland, Yamaha, Casio, Kawai, and other USB MIDI instruments.
- Bluetooth LE MIDI output to iOS, iPadOS, macOS, Android, and desktop apps.
- A visible startup/status screen on the Espressif ESP32-S3-USB-OTG board.
- Reproducible Arduino CLI builds for both supported firmware paths.

## Current Limits

- MIDI is currently one-way: piano to Bluetooth MIDI app.
- Bluetooth audio is not supported.
- Some keyboards need a real USB host power source on VBUS before they enumerate.
- BLE MIDI latency depends on the receiving device and app.

## Recommended Hardware

### Best Path: ESP32-S3 Native USB Host

Use an ESP32-S3 board that exposes USB-OTG host D+/D- and can power the attached
USB MIDI device. This repo has been set up for the official Espressif
ESP32-S3-USB-OTG Development Board.

For that board:

- Use the Micro-USB `USB-to-UART` port for flashing and logs.
- Use the Type-A host port for the piano or MIDI controller.
- Power the board so the host port can supply 5 V VBUS to the MIDI device.
- The display shows the Bluetooth name and live USB/BLE status.

### Fallback: Classic ESP32 + MAX3421E

Classic ESP32 boards do not have a native USB host peripheral. Their USB
connector is only for flashing/logging. To use one, add a MAX3421E USB host
module and build the fallback sketch in
`Classic-ESP32-MAX3421E-MIDI-BLE`.

## Firmware Choices

| Folder | Hardware | Use when |
| --- | --- | --- |
| `USB-MIDI-BLE-Bridge` | ESP32-S3 with native USB-OTG host | You have an ESP32-S3 USB host board |
| `Classic-ESP32-MAX3421E-MIDI-BLE` | Classic ESP32 + MAX3421E host module | You have a non-S3 ESP32 board |
| `CT-USB-BLE` | Legacy/raw experiment | Kept for reference |

## Bluetooth Name

The default BLE MIDI name is:

```text
Piano BLE Bridge
```

On the ESP32-S3-USB-OTG display build, this same name is printed on the screen at
boot. To change it, edit `BLE_DEVICE_NAME_TEXT` in the sketch or pass a compiler
define in your build flags.

## Build With Arduino CLI

Install Arduino CLI, then install the ESP32 core and libraries:

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install FastLED "USB Host Shield Library 2.0" "GFX Library for Arduino"
```

Build the ESP32-S3 native USB host firmware:

```bash
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

Upload to the Espressif ESP32-S3-USB-OTG board:

```bash
arduino-cli upload \
  -p /dev/cu.usbserial-1110 \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

Build the classic ESP32 fallback:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 ./Classic-ESP32-MAX3421E-MIDI-BLE
```

## Pairing

1. Flash the correct firmware for your hardware.
2. Plug the piano or MIDI controller into the ESP32 host port.
3. Power the ESP32 bridge.
4. Open a MIDI-capable app.
5. Use the app's Bluetooth MIDI device menu and connect to `Piano BLE Bridge`.
6. Play notes and confirm the app receives MIDI input.

On iOS and iPadOS, BLE MIDI devices are usually paired inside music apps, not in
the normal system Bluetooth settings.

## Troubleshooting

### The Piano Does Not Connect Over USB

- Confirm the piano is class-compliant USB MIDI.
- Confirm the ESP32 host port is supplying 5 V VBUS.
- Try another known data-capable USB cable.
- For ESP32-S3-USB-OTG, use the Type-A host port for the piano and the Micro-USB
  USB-to-UART port for flashing/logs.

### No Bluetooth MIDI Device Appears

- Confirm the firmware booted. On the ESP32-S3-USB-OTG board, the display should
  show `Piano BLE Bridge`.
- Open the Bluetooth MIDI device menu inside your music app.
- Restart the ESP32 after changing BLE settings or names.

### Classic ESP32 Board Does Not See USB MIDI

A classic ESP32 cannot become a USB host in firmware. Use the MAX3421E fallback
sketch or switch to an ESP32-S3 native USB host board.

## Development

GitHub Actions compile the supported sketches on every push and pull request.
See [BUILD.md](BUILD.md) for board-specific notes.

## Credits

This project builds on the ESP32 USB MIDI host work by
[sauloverissimo](https://github.com/sauloverissimo/ESP32_Host_MIDI) and later
keyboard bridge work by Liam Jones.

## License

MIT. See [LICENSE.txt](LICENSE.txt).
