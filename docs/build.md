# Build Notes

## Tooling

- Arduino CLI (tested with 1.5.x)
- Arduino ESP32 core 3.3.x (`esp32:esp32`)
- Libraries:
  - `USB Host Shield Library 2.0` (classic MAX3421E fallback only)
  - `GFX Library for Arduino` (ESP32-S3 display build only)

## Helper scripts

| Script | Purpose |
|--------|---------|
| [`scripts/flash-bridge-s3.sh`](../scripts/flash-bridge-s3.sh) | Compile, upload, post-flash `esptool run`; checks port is free |
| [`scripts/flash-bridge-s3-ota.sh`](../scripts/flash-bridge-s3-ota.sh) | Compile and upload over Wi-Fi OTA after provisioning |
| [`scripts/verify-boot.sh`](../scripts/verify-boot.sh) | Capture boot log; verify LCD/canvas markers (`--flash` to flash first) |
| [`read_serial.py`](../read_serial.py) | Stream USB Serial/JTAG logs; `--reset` for watchdog reset; close before flash |
| [`scripts/wifi_log.py`](../scripts/wifi_log.py) | Receive Wi-Fi UDP debug logs on port 3333 (`ENABLE_WIFI_DEBUG=1`) |
| [`scripts/probe-ble-midi.sh`](../scripts/probe-ble-midi.sh) | macOS BLE MIDI probe; subscribes to bridge notifications and can send a test note |
| [`scripts/run-hardware-diagnostics.sh`](../scripts/run-hardware-diagnostics.sh) | Compile or flash/probe USB host rail diagnostic builds; writes ignored evidence logs |
| [`scripts/analyze-hardware-diagnostics.py`](../scripts/analyze-hardware-diagnostics.py) | Summarize hardware diagnostic logs into stage/rail/BLE/USB evidence lines |
| [`scripts/test.sh`](../scripts/test.sh) | Host-side unit tests |

**Standard FQBN** (use everywhere for ESP32-S3-USB-OTG):

```text
esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc
```

## Product Firmware (ESP32-S3)

Official Espressif **ESP32-S3-USB-OTG** board — 8 MB flash, no external PSRAM:

```bash
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3

arduino-cli upload \
  -p /dev/cu.usbmodem11101 \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

Do **not** use `PSRAM=enabled` on this board — boot logs show
`quad_psram: PSRAM chip is not connected`.

Replace the port with your board (`arduino-cli board list`).

## Browser Flashing

The latest `main` build is published as a GitHub Pages web flasher:

```text
https://guitarbeat.github.io/esp32-midi-bridge/
```

Use desktop Chrome or Edge. Safari, Firefox, and iOS browsers do not reliably
support the Web Serial flow used by ESP Web Tools. Connect the ESP32-S3-USB-OTG
board through its USB Serial/JTAG port, close Arduino Serial Monitor and
`read_serial.py`, then choose the serial port in the browser.

The web flasher is only for the official ESP32-S3-USB-OTG product firmware
(`PartitionScheme=default_8MB,USBMode=hwcdc`, no PSRAM). It does not publish the
classic ESP32/MAX3421E fallback firmware to avoid accidental wrong-board flashes.

If the board is not visible as a serial device to the browser, the web flasher
cannot recover it. Use the command-line recovery and manual BOOT/RESET workflow
below.

**After USB Serial/JTAG upload:** the default Arduino reset often leaves the chip in
**download mode** (`waiting for download` in serial, blank display). Either:

- Press **RESET** once (do **not** hold BOOT), or
- Use the helper script (recommended):

```bash
./scripts/flash-bridge-s3.sh
```

The flash helper uploads the exact binary it just compiled. Optional local
defines can be passed with `BUILD_DEFINES`:

```bash
BUILD_DEFINES='ENABLE_BLE_DIAGNOSTICS=1' ./scripts/flash-bridge-s3.sh
```

To verify the exact helper build without uploading:

```bash
COMPILE_ONLY=1 BUILD_DEFINES='ENABLE_BLE_DIAGNOSTICS=1' ./scripts/flash-bridge-s3.sh
```

Use this for short diagnostic sessions only. BLE diagnostics send text
notifications on the BLE MIDI characteristic, which is useful for
`scripts/probe-ble-midi.sh` but not appropriate for normal DAW use.

**Before uploading:** close Serial Monitor, `read_serial.py`, and any other app
using the USB port. A background serial reader causes uploads to fail around
80–280 KB with errors like “chip stopped responding” or “serial data stream stopped”.

See also: [ESP32-S3 flash/display bring-up troubleshooting](solutions/integration-issues/esp32-s3-usb-otg-flash-display-bringup.md).

### Boot verification checklist

After flashing, confirm the app is running (not stuck in download mode):

```bash
./scripts/verify-boot.sh          # capture serial + check markers
./scripts/verify-boot.sh --flash  # flash then verify
python3 read_serial.py --reset    # watchdog reset, then stream logs
```

Required serial markers:

- `[LCD] display->begin OK`
- `[SYSTEM] Display canvas initialized.`

Must **not** appear: `waiting for download`, `quad_psram`

### Optional: Wi-Fi debug logging

When USB host mode is active, native USB CDC (`usbmodem`) may stop because D+/D− are switched to the Type-A host port. For runtime diagnostics, enable Wi-Fi UDP logging (Omocha-style, port **3333**):

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DENABLE_WIFI_DEBUG=1' \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

On your Mac (same LAN as the board, after Wi-Fi provisioning):

```bash
python3 scripts/wifi_log.py
```

USB/BLE/SYSTEM log lines that use `BRIDGE_LOG` in firmware are mirrored over UDP when Wi-Fi is connected.

### BLE-first USB host startup

`bridge-s3` starts BLE advertising before enabling USB host rails. By default it
waits up to 15 seconds for a BLE MIDI notify subscription before starting USB
host mode; if no BLE client subscribes, USB host starts after the timeout so the
bridge still works standalone. This avoids the observed ESP32-S3-USB-OTG failure
mode where starting USB host immediately leaves the BLE peripheral advertising
but unable to complete a macOS CoreBluetooth connection.

For diagnosis, tune the window with:

```bash
BUILD_DEFINES='USB_HOST_DEFER_UNTIL_BLE_SUBSCRIBE_MS=30000 ENABLE_BLE_DIAGNOSTICS=1' \
  ./scripts/flash-bridge-s3.sh
```

After flashing that build, run the probe within the defer window:

```bash
./scripts/probe-ble-midi.sh --duration 60
```

Expected sequence: `Discovered`, `Connecting`, `Connected`, `Subscribing`,
`Notify subscribed=true`, then periodic `DIAG USB ...` lines after USB host
starts. If connection succeeds before USB host starts but times out immediately
after USB host starts, continue investigating USB host mux/power and scheduler
interaction.

USB host startup isolation matrix:

```bash
# Compile all diagnostic rail combinations without flashing; no board required.
./scripts/run-hardware-diagnostics.sh --compile-only

# Flash one case and capture boot + BLE probe evidence.
./scripts/run-hardware-diagnostics.sh --flash --case sel-only --port /dev/cu.usbmodem1101

# Run the full hardware matrix when you can supervise the connected board.
./scripts/run-hardware-diagnostics.sh --flash --port /dev/cu.usbmodem1101 --probe-duration 75
```

Diagnostic logs are written to `diagnostics/<timestamp>/` and are gitignored.
Cases: `no-rails`, `sel-only`, `rails-no-sel`, `vbus-only`, `limit-only`,
`boost-only`, and `normal`. Each build advertises BLE first, waits for a notify
subscription, delays 5 seconds, then starts USB host. BLE diagnostic
notifications include the USB host stage and active rail config. The runner
appends one analyzer line per flashed case; rerun the analyzer manually with:

```bash
./scripts/analyze-hardware-diagnostics.py diagnostics/rails-no-sel-run
```

Observed so far: BLE stayed connected and emitted diagnostics when USB host was
installed with `USB_HOST_ENABLE_POWER_RAILS=0`. BLE also stayed connected for a
full 75 second `rails-no-sel` probe (`SEL=0 VBUS=1 LIMIT=1 BOOST=1`) with 73
diagnostic notifications and no USB device expected. A `sel-only` probe stayed
connected until it was interrupted. BLE timed out when the normal host rails/mux
path was enabled, so the next evidence to capture is the full `normal` case with
fresh diagnostics, then individual rails only if needed.

### Roland F-20 debugging note

The F-20 owner manual (`F-20_egfispd01_W.pdf`, if present locally under
`local-artifacts/manuals/`) confirms the F-20 **USB COMPUTER** port is the
computer/sequencer MIDI port. The **USB MEMORY** port is for flash drives or the
Roland wireless USB adapter and should not be connected to the ESP32 host for
MIDI bridging.

The MIDI Implementation reference (`F-20_MI.pdf`, if present locally under
`local-artifacts/manuals/`) is not a USB descriptor reference. It confirms the
F-20 should transmit standard MIDI 1.0 messages after a MIDI transport exists:
Note On/Off, Bank Select, Program Change, Hold CC64, Sostenuto CC66, Soft CC67,
Reverb CC91, and Identity Reply SysEx. The current bridge parser already handles
those short messages.

If the display reports `No USB MIDI interface`, `USB WAIT`, or `USB NOMID`, debug
USB enumeration first. Connect the Roland **USB COMPUTER** port directly to a Mac
and verify it appears in **Audio MIDI Setup -> MIDI Studio**. An F-20 observed as
**Roland Digital Piano** on macOS reports `VID:PID 0582:0122`; the S3 firmware
accepts that Roland vendor interface and falls back to byte-stream MIDI decoding
if standard USB-MIDI event packets are not present. See
[`docs/solutions/integration-issues/roland-f20-usb-midi-diagnostics.md`](solutions/integration-issues/roland-f20-usb-midi-diagnostics.md).

### Prebuilt binary

CI builds `./firmware/bridge-s3`, publishes the browser-flash merged binary to
GitHub Pages on `main`, and uploads `bridge-s3.ino.bin` plus the web-flash image
as workflow artifacts on each push. Download them from the Actions tab for your
branch when you need manual flashing.

Flash with `esptool.py` or the Arduino IDE “Flash from file” if you use a third-party
flasher; match the same board/partition settings as above.

### Hub mode and USB reverse path

Default `bridge-s3` firmware runs in hub mode:

- USB host MIDI input broadcasts to BLE, RTP-MIDI, and optional UART outputs.
- BLE, RTP-MIDI, and optional UART input can write back to the USB keyboard when
  the keyboard exposes a USB MIDI OUT endpoint.
- The display shows **USB OUT READY** when that reverse path is available and
  **USB OUT N/A** when the attached keyboard is input-only.

This is not native USB-device passthrough to a Mac/iPad on the ESP32-S3-USB-OTG
board. The native USB peripheral is consumed by host mode; use BLE, RTP-MIDI, or
UART as the downstream app/computer path unless you change hardware topology.

MIDI Clock is filtered by default to avoid flooding receivers. To forward it:

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DENABLE_MIDI_CLOCK_PASSTHROUGH=1' \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

### Optional: USB debug logging

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DDEBUG_USB=1' \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

### On-board controls (ESP32-S3-USB-OTG)

| Button | GPIO | Action |
|--------|------|--------|
| **UP+** | 10 | Transpose +1 semitone |
| **DW-** | 11 | Transpose −1 semitone |
| **MENU** | 14 | Tap: cycle MIDI channel filter (all → ch1…ch16) |
| **MENU** (hold ~1 s) | 14 | Cycle backlight dim timeout (30s / 90s / 3m / never) |
| **MENU** (hold ~4 s) | 14 | Open WiFi setup AP (captive portal) |
| **OK** / Boot | 0 | Tap: confirm unified view |
| **OK** (hold ~1 s) | 0 | Send **All Notes Off** on BLE (panic) |
| **OK** (hold ~2.5 s) | 0 | Pause / resume USB→BLE forwarding |

A short toast appears at the bottom of the display when a setting changes.

On boards without the side buttons, set unused pins to `-1` via build flags
(e.g. `-DBOARD_BTN_UP=-1`).

Settings persist in NVS. Changing the BLE name in NVS requires a reboot to take effect (edit via reflash with `BLE_DEVICE_NAME_TEXT` or future tooling).

Unplugging the USB keyboard no longer reboots the board — plug the piano back in; BLE MIDI should stay connected in your app.

The display distinguishes BLE radio connection from BLE MIDI readiness. **BLE OPEN APP**
means a central device connected but has not subscribed to MIDI notifications yet; open
the MIDI app's Bluetooth MIDI device list and connect there.

Backlight dims after the configured idle period; any MIDI activity wakes it.

### BLE MIDI probe

On macOS, use the local probe script to test the bridge without opening a DAW:

```bash
./scripts/probe-ble-midi.sh --duration 30
./scripts/probe-ble-midi.sh --duration 15 --send-note
```

The first command subscribes to BLE MIDI notifications; play notes on the piano and
watch for `MIDI_NOTIFY` lines. The second also sends two middle-C notes from the Mac
through BLE to the bridge, which tests the reverse path to USB MIDI OUT when the
keyboard exposes one.

### Custom BLE name

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DBLE_DEVICE_NAME_TEXT=\"My Piano Bridge\"' \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

### WiFi RTP-MIDI (Apple MIDI)

Enabled by default. Routes USB MIDI to **RTP-MIDI on port 5004** while BLE stays active, and accepts inbound RTP-MIDI short messages as another bridge source. Uses the [AppleMIDI](https://github.com/lathoub/Arduino-AppleMidi-Library) library.

1. Install: `arduino-cli lib install AppleMIDI`
2. Flash firmware (RTP is on unless you set `ENABLE_RTP_MIDI` to `0` in `NetworkConfig.h`).
3. **First-time WiFi setup** (no credentials saved yet, or saved network unreachable):
   - The board opens a setup WiFi network named **`Piano-BLE-Bridge-Setup`** (spaces in the BLE name become dashes).
   - On your phone or laptop, join that network (open / no password).
   - A captive portal should open; if not, browse to **http://192.168.4.1**
   - Pick your home WiFi, enter the password, and tap **Save and connect**. The board reboots and joins your LAN.
4. **Re-open setup later:** hold **MENU** for ~4 seconds on the ESP32-S3-USB-OTG board.
5. On macOS: **Audio MIDI Setup → MIDI Studio → Network** — add a session with the bridge’s IP (shown on the unified display when Wi-Fi is up) and port **5004**.

Optional compile-time fallback (skips the portal on first boot if NVS is empty): copy `wifi_secrets.example.h` to `wifi_secrets.h` and set `WIFI_SSID_TEXT` / `WIFI_PASSWORD_TEXT` before building.

Wi-Fi uses DHCP. RTP forwards only after a host connects to the RTP session.
Inbound RTP-MIDI handles Note On/Off, CC, Program Change, Channel Pressure, Pitch Bend, and Start/Stop/Continue.

To disable RTP-MIDI (BLE-only build), set `#define ENABLE_RTP_MIDI 0` in `NetworkConfig.h`.

### Over-the-air (OTA) firmware updates

When the board is on your WiFi (after RTP setup), you can flash new firmware **without USB**:

1. Board and computer must be on the **same LAN**.
2. Serial log on boot prints `[OTA] Ready at piano-ble-bridge.local (port 3232)` (hostname derived from the BLE name).
3. Upload from this repo:

```bash
# Helper script (default target: esp32-midi-bridge.local)
./scripts/flash-bridge-s3-ota.sh

# Or choose a hostname/IP explicitly
./scripts/flash-bridge-s3-ota.sh piano-ble-bridge.local
./scripts/flash-bridge-s3-ota.sh 192.168.1.42

# By mDNS hostname (macOS usually resolves .local)
arduino-cli upload \
  -p piano-ble-bridge.local \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3

# Or use the IP shown on the display (RTP x.x.x.x line)
arduino-cli upload \
  -p 192.168.1.42 \
  --fqbn 'esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc' \
  ./firmware/bridge-s3
```

4. `arduino-cli board list` may also show a **network port** for the ESP32 when OTA is advertising.

Optional OTA password: copy `ota_secrets.example.h` to `ota_secrets.h` and set `OTA_PASSWORD_TEXT`, then pass
`--upload-property upload.password=YourPassword` on upload, or run the helper with:

```bash
OTA_PASSWORD_TEXT='YourPassword' ./scripts/flash-bridge-s3-ota.sh 192.168.1.42
```

Disable OTA with `#define ENABLE_OTA 0` in `NetworkConfig.h` (RTP/BLE still work).

**Note:** The first flash after adding OTA still needs USB once. After that, use OTA for routine updates.

## Unit Tests

Logic tests that don't depend on hardware can be run on the host machine using `g++`:

```bash
./scripts/test.sh
```

These tests are located in the `test/` directory and use a mock for Arduino-specific types.

## Fallback Firmware (Classic ESP32 + MAX3421E)

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 ./firmware/bridge-classic
```

Requires the USB Host Shield library and wired MAX3421E module.

## Recovery (ESP32-S3-USB-OTG)

If the board disappears from `/dev/cu.*` after USB host firmware runs:

1. Hold **BOOT**
2. Press and release **RESET**
3. Release **BOOT**
4. Flash again using the **USB-to-UART** Micro-USB port

### Upload drops mid-flash (~80–280 KB)

1. **Close other serial clients** — only one process may use `/dev/cu.usbmodem*`.
   Check with `lsof /dev/cu.usbmodem11101` (adjust port name).
2. **Do not use GPIO 43/44 for UART MIDI** — they are the USB Serial/JTAG pins.
   This firmware uses GPIO 47 (TX) / 48 (RX) when UART MIDI is enabled.
3. If uploads still fail, try a shorter data-capable USB cable or lower speed:
   `--upload-property upload.speed=460800`
4. Manual download mode: hold **BOOT**, tap **RESET**, release **BOOT**, then upload.

### Boot reboot loop after `[LCD] display->begin OK`

Serial repeats `ESP-ROM` and never reaches `[SYSTEM] Display canvas initialized.` — usually USB host rails were enabled inside `Board::begin()`. Current firmware defers them to `enableUsbHostPower()` (called from `UsbMidiHost::begin()` after canvas init). Reflash with `./scripts/flash-bridge-s3.sh` and verify with `./scripts/verify-boot.sh`.

## USB host power pins (ESP32-S3-USB-OTG)

`UsbMidiHost::begin()` calls `Board::enableUsbHostPower()` after `usb_host_install`:

| GPIO | Role |
| --- | --- |
| 18 | High = Type-A host mux selected (D+/D− routed to host port) |
| 12 | High = host VBUS from USB_DEV power path |
| 17 | High = current-limited host power switch |
| 13 | Low = battery boost disabled (default) |

Display init in `Board::begin()` runs **before** USB host rails. LCD enable is GPIO 5 (LOW); backlight is GPIO 9.

Micro-USB / USB Serial/JTAG can power the MCU and display while the Type-A port still needs VBUS from
**USB_DEV** or battery for many keyboards. After host mux switches, CDC logging may stop — use Wi-Fi debug (above).

## Hardware reality check

Web flasher or serial output showing `Chip type: ESP32` (classic) means you need the
MAX3421E fallback sketch, not the S3 native USB host sketch.
