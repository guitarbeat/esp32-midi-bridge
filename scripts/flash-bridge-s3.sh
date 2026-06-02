#!/usr/bin/env bash
# Flash ESP32-S3-USB-OTG (no PSRAM) and boot the app after USB Serial/JTAG upload.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN='esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc'
PORT="${1:-}"
BUILD_DEFINES="${BUILD_DEFINES:-}"
COMPILE_ONLY="${COMPILE_ONLY:-0}"
UPLOAD_RETRIES="${UPLOAD_RETRIES:-3}"
if [[ -z "${BUILD_PATH:-}" ]]; then
  BUILD_PATH="$ROOT/firmware/bridge-s3/build/arduino-cli"
fi
mkdir -p "$BUILD_PATH"

LOCAL_BUILD_CONFIG="$ROOT/firmware/bridge-s3/LocalBuildConfig.h"
LOCAL_BUILD_CONFIG_BACKUP=""
restore_local_build_config() {
  if [[ -n "$LOCAL_BUILD_CONFIG_BACKUP" && -e "$LOCAL_BUILD_CONFIG_BACKUP" ]]; then
    mv "$LOCAL_BUILD_CONFIG_BACKUP" "$LOCAL_BUILD_CONFIG"
  elif [[ -n "$BUILD_DEFINES" ]]; then
    rm -f "$LOCAL_BUILD_CONFIG"
  fi
}
trap restore_local_build_config EXIT

write_local_build_config() {
  if [[ -z "$BUILD_DEFINES" ]]; then
    return
  fi

  if [[ -e "$LOCAL_BUILD_CONFIG" ]]; then
    LOCAL_BUILD_CONFIG_BACKUP="$(mktemp "${TMPDIR:-/tmp}/LocalBuildConfig.XXXXXX")"
    cp "$LOCAL_BUILD_CONFIG" "$LOCAL_BUILD_CONFIG_BACKUP"
  fi

  {
    echo "#ifndef LOCAL_BUILD_CONFIG_H"
    echo "#define LOCAL_BUILD_CONFIG_H"
    for define in $BUILD_DEFINES; do
      define="${define#-D}"
      if [[ "$define" == *=* ]]; then
        echo "#define ${define%%=*} ${define#*=}"
      else
        echo "#define $define 1"
      fi
    done
    echo "#endif  // LOCAL_BUILD_CONFIG_H"
  } > "$LOCAL_BUILD_CONFIG"
}

find_upload_port() {
  arduino-cli board list | awk '/usbmodem/ {print $1; exit}'
}

wait_for_upload_port() {
  local timeout="${1:-30}"
  local start
  start="$(date +%s)"

  while true; do
    PORT="$(find_upload_port)"
    if [[ -n "$PORT" && -e "$PORT" ]]; then
      return 0
    fi

    if (( "$(date +%s)" - start >= timeout )); then
      return 1
    fi

    sleep 1
  done
}

if [[ -z "$PORT" && "$COMPILE_ONLY" != "1" ]]; then
  wait_for_upload_port 10 || true
fi

if [[ -z "$PORT" && "$COMPILE_ONLY" != "1" ]]; then
  echo "No /dev/cu.usbmodem* port found. Plug in the board and retry." >&2
  exit 1
fi

if [[ -n "$PORT" ]] && PIDS="$(lsof -t "$PORT" 2>/dev/null)"; then
  echo "Port $PORT is busy (PID(s): $PIDS)." >&2
  echo "Close read_serial.py, Serial Monitor, and any other serial client, then retry." >&2
  exit 1
fi

if [[ -n "$PORT" ]]; then
  echo "Port: $PORT"
else
  echo "Port: (not required for compile-only)"
fi
echo "Compiling ($FQBN)..."
if [[ -n "$BUILD_DEFINES" ]]; then
  echo "Build defines: $BUILD_DEFINES"
fi
write_local_build_config
compile_args=(compile --fqbn "$FQBN" --build-path "$BUILD_PATH")
arduino-cli "${compile_args[@]}" "$ROOT/firmware/bridge-s3"

if [[ "$COMPILE_ONLY" == "1" ]]; then
  echo "Compile-only mode complete. Build path: $BUILD_PATH"
  exit 0
fi

ESPTOOL="$(find "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py" -name esptool -type f 2>/dev/null | sort -V | tail -1)"
if [[ -z "$ESPTOOL" ]]; then
  echo "esptool not found. Install esp32:esp32 core via arduino-cli." >&2
  exit 1
fi

if [[ ! -e "$PORT" ]]; then
  echo "Waiting for upload port to reappear..."
  wait_for_upload_port 30 || {
    echo "No /dev/cu.usbmodem* port found. Hold BOOT while reconnecting USB, then retry." >&2
    exit 1
  }
  echo "Port: $PORT"
fi

echo "Uploading..."
upload_ok=0
for attempt in $(seq 1 "$UPLOAD_RETRIES"); do
  if [[ ! -e "$PORT" ]]; then
    echo "Waiting for upload port to reappear..."
    wait_for_upload_port 30 || true
  fi

  if [[ -z "$PORT" || ! -e "$PORT" ]]; then
    echo "Upload attempt $attempt/$UPLOAD_RETRIES: no upload port."
  else
    echo "Upload attempt $attempt/$UPLOAD_RETRIES on $PORT..."
    if arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD_PATH" "$ROOT/firmware/bridge-s3"; then
      upload_ok=1
      break
    fi
  fi

  sleep 2
  PORT="$(find_upload_port)"
done

if [[ "$upload_ok" != "1" ]]; then
  echo "Upload failed. Hold BOOT while reconnecting USB, then rerun this command." >&2
  exit 1
fi

echo "Waiting for USB reconnect..."
sleep 4
PORT="$(find_upload_port)"
if [[ -n "$PORT" ]]; then
  echo "Starting flashed app (watchdog reset) on $PORT..."
  # Use `run`, not chip-id — chip-id uploads the stub and often re-enters download mode.
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --before no-reset --after watchdog-reset run >/dev/null 2>&1 || true
fi

echo ""
echo "Flashed. Press RESET once if the display stays blank."
echo "Then run: python3 read_serial.py"
echo "Expect: [LCD] display->begin OK, Display canvas initialized."
