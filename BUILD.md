# Build Notes

## Tooling Installed

- Arduino CLI `1.5.0`
- PlatformIO Core `6.1.19`
- Arduino ESP32 core `3.3.8`
- Arduino libraries:
  - `USB Host Shield Library 2.0` `1.7.0`
  - `FastLED` `3.10.3`
  - `GFX Library for Arduino` `1.6.5`

## Verified Arduino CLI Builds

Classic ESP32 with external MAX3421E USB host module:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .\Classic-ESP32-MAX3421E-MIDI-BLE
```

ESP32-S3 with native USB-OTG host:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32s3 .\USB-MIDI-BLE-Bridge
```

Both builds passed locally after installing the ESP32 Arduino core and dependencies.

## ESP32-S3-USB-OTG Development Board

For the official Espressif ESP32-S3-USB-OTG board, build the native USB host
sketch with the detected 8 MB flash layout and USB CDC serial enabled:

```bash
arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' ./USB-MIDI-BLE-Bridge
arduino-cli upload -p /dev/cu.usbmodem11101 --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' ./USB-MIDI-BLE-Bridge
```

If the board disappears from `/dev/cu.*` after running USB host firmware, put it
back in download mode: hold `BOOT`, press and release `RESET`, then release
`BOOT`. Use the board's Micro-USB `USB-to-UART` port for flashing and serial
logs.

The sketch enables the board's USB host mux/power pins before starting the USB
host stack:

- `GPIO18` high selects the Type-A USB host connector.
- `GPIO12` high enables host VBUS from the USB device power path.
- `GPIO17` high enables the current-limited host power switch.
- `GPIO13` low leaves battery boost disabled.

For a USB MIDI device on the Type-A host port, the board also needs a 5 V source
on the USB device/power path or battery power; Micro-USB debug power alone does
not power every external host-device setup.

If Bluetooth connects but no MIDI arrives, check this first: the bridge firmware
can run from Micro-USB debug power while the Type-A `USB HOST` port has no VBUS
for the piano. The default build expects 5 V on the `USB_DEV` port.

## Hardware Reality Check

The observed webflasher output reports `Chip type ESP32`, which is a classic ESP32. It can run BLE MIDI, but it cannot directly host a USB MIDI keyboard from its onboard USB/serial connector. Use either:

- the classic ESP32 sketch with an external MAX3421E USB host module, or
- the ESP32-S3 sketch with a board that exposes native USB-OTG host.
