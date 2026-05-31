## Learned User Preferences

- Verify with runtime evidence (successful flash, serial boot log, display output) before claiming something is fixed or working.
- Prefer simple, user-friendly, buildable, robust open firmware that anyone can flash for USB MIDI to Bluetooth LE MIDI.
- README should explain project motivation as an open replacement for commercial USB-to-BLE MIDI dongles (e.g. DoReMiDi UTB-21 Pro).

## Learned Workspace Facts

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
