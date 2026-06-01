#!/usr/bin/env bash
# Flash ESP32_Host_MIDI USB->BLE bridge for ESP32-S3-USB-OTG (USB host mode).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN='esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=default'
PORT="${1:-}"

if [[ -z "$PORT" ]]; then
  PORT="$(arduino-cli board list | awk '/usbmodem/ {print $1; exit}')"
fi

if [[ -z "$PORT" ]]; then
  echo "No /dev/cu.usbmodem* port found. Plug in the board and retry." >&2
  exit 1
fi

if PIDS="$(lsof -t "$PORT" 2>/dev/null)"; then
  echo "Port $PORT is busy (PID(s): $PIDS)." >&2
  echo "Close read_serial.py, Serial Monitor, and any other serial client, then retry." >&2
  exit 1
fi

if ! arduino-cli lib list | grep -q 'ESP32_Host_MIDI'; then
  echo "Installing ESP32_Host_MIDI library..."
  arduino-cli lib install "ESP32_Host_MIDI"
fi

if ! arduino-cli lib list | grep -q 'GFX Library for Arduino'; then
  echo "Installing GFX Library for Arduino..."
  arduino-cli lib install "GFX Library for Arduino"
fi

ESPTOOL="$(find "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py" -name esptool -type f 2>/dev/null | sort -V | tail -1)"

echo "Port: $PORT"
echo "Compiling ($FQBN)..."
arduino-cli compile --fqbn "$FQBN" "$ROOT/firmware/host-midi-usb-ble"

echo "Uploading..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$ROOT/firmware/host-midi-usb-ble"

echo "Waiting for USB reconnect..."
sleep 4
PORT="$(arduino-cli board list | awk '/usbmodem/ {print $1; exit}')"
if [[ -n "$PORT" && -n "$ESPTOOL" ]]; then
  echo "Starting flashed app (watchdog reset) on $PORT..."
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --before no-reset --after watchdog-reset run >/dev/null 2>&1 || true
fi

echo ""
echo "Flashed. Connect BLE to \"Piano BLE Bridge\"."
echo "Keyboard on Type-A host port; enable USB_DEV power if the piano does not enumerate."
