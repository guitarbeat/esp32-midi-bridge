# Piano BLE Bridge

USB MIDI piano or controller -> ESP32-S3 bridge -> Bluetooth LE MIDI / RTP-MIDI.

This is open firmware for turning an ESP32-S3-USB-OTG board into a USB MIDI host
and wireless MIDI bridge. It was built around a Roland F-20 -> iPad/GarageBand
use case, but should work with class-compliant USB MIDI devices and known vendor
fallbacks.

## Flash

For the official Espressif ESP32-S3-USB-OTG board, use the browser flasher:

https://guitarbeat.github.io/esp32-midi-bridge/

Command-line fallback:

```bash
./scripts/flash-bridge-s3.sh
./scripts/verify-boot.sh
```

Required ESP32-S3 FQBN:

```text
esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc
```

Do not enable PSRAM on the ESP32-S3-USB-OTG board.

## Hardware

- Primary target: Espressif ESP32-S3-USB-OTG, 8 MB flash, no PSRAM.
- Connect the piano/controller to the Type-A USB HOST port.
- Power `USB_DEV` so the host port can provide 5 V VBUS.
- Classic ESP32 boards need the separate MAX3421E fallback firmware in
  `firmware/bridge-classic/`.

## What Works

- USB MIDI input to BLE MIDI.
- RTP-MIDI over Wi-Fi alongside BLE.
- BLE/RTP/UART input back to USB MIDI OUT when the keyboard exposes an OUT
  endpoint.
- Display status, mini keyboard, velocity, transport counters, and diagnostics.
- OTA updates after Wi-Fi provisioning.

## Development

| Task | Command |
|------|---------|
| Flash S3 | `./scripts/flash-bridge-s3.sh` |
| Verify boot | `./scripts/verify-boot.sh` |
| Unit tests | `./scripts/test.sh` |
| Build web flasher | `./scripts/build-web-flasher.sh` |
| BLE probe | `./scripts/probe-ble-midi.sh --duration 75` |

Detailed docs:

- Build, flashing, OTA, diagnostics: [docs/build.md](docs/build.md)
- Project docs index: [docs/README.md](docs/README.md)
- Agent instructions: [docs/agent-instructions.md](docs/agent-instructions.md)
- Architecture vocabulary: [docs/context.md](docs/context.md)
- Roland F-20 notes:
  [docs/solutions/integration-issues/roland-f20-usb-midi-diagnostics.md](docs/solutions/integration-issues/roland-f20-usb-midi-diagnostics.md)

## License

MIT. See [LICENSE.txt](LICENSE.txt).
