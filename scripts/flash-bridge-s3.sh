#!/usr/bin/env bash
# Flash ESP32-S3-USB-OTG (no PSRAM) and boot the app after USB Serial/JTAG upload.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN='esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc'
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

ESPTOOL="$(find "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py" -name esptool -type f 2>/dev/null | sort -V | tail -1)"
if [[ -z "$ESPTOOL" ]]; then
  echo "esptool not found. Install esp32:esp32 core via arduino-cli." >&2
  exit 1
fi

echo "Port: $PORT"
echo "Compiling ($FQBN)..."
arduino-cli compile --fqbn "$FQBN" "$ROOT/firmware/bridge-s3"

echo "Uploading..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$ROOT/firmware/bridge-s3"

echo "Waiting for USB reconnect..."
sleep 4
PORT="$(arduino-cli board list | awk '/usbmodem/ {print $1; exit}')"
if [[ -n "$PORT" ]]; then
  echo "Starting flashed app (watchdog reset) on $PORT..."
  # Use `run`, not chip-id — chip-id uploads the stub and often re-enters download mode.
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --before no-reset --after watchdog-reset run >/dev/null 2>&1 || true
fi

echo ""
echo "Flashed. Press RESET once if the display stays blank."
echo "Then run: python3 read_serial.py"
echo "Expect: [LCD] display->begin OK, Display canvas initialized."
