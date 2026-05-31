#!/usr/bin/env bash
# Capture ESP32-S3-USB-OTG boot log and verify LCD/canvas markers.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CAPTURE_SECS="${CAPTURE_SECS:-15}"

if [[ "${1:-}" == "--flash" ]]; then
  shift
  "$ROOT/scripts/flash-bridge-s3.sh" "${1:-}"
  sleep 2
fi

PORT=""
for _ in $(seq 1 30); do
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
  if [[ -n "$PORT" ]]; then
    break
  fi
  sleep 1
done

if [[ -z "${PORT:-}" ]]; then
  echo "No /dev/cu.usbmodem* port found within 30s. Plug in the board and retry." >&2
  exit 1
fi

if PIDS="$(lsof -t "$PORT" 2>/dev/null)"; then
  echo "Port $PORT is busy (PID(s): $PIDS). Close read_serial.py first." >&2
  exit 1
fi

echo "Capturing boot log from $PORT for ${CAPTURE_SECS}s..."
LOG="$(python3 - "$PORT" "$CAPTURE_SECS" <<'PY'
import sys, serial, time

port, secs = sys.argv[1], float(sys.argv[2])
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.3
ser.dtr = False
ser.rts = False
ser.open()
time.sleep(0.5)
end = time.time() + secs
chunks = []
while time.time() < end:
    data = ser.read(4096)
    if data:
        chunks.append(data)
ser.close()
print(b"".join(chunks).decode("utf-8", errors="replace"), end="")
PY
)"

echo "=== BOOT LOG ==="
if [[ -z "${LOG//[[:space:]]/}" ]]; then
  echo "(empty — press RESET once and rerun, or use: ./scripts/verify-boot.sh --flash)"
else
  echo "$LOG"
fi

FAIL=0
check_absent() {
  if echo "$LOG" | grep -q "$1"; then
    echo "FAIL: found forbidden marker: $1" >&2
    FAIL=1
  fi
}
check_present() {
  if ! echo "$LOG" | grep -q "$1"; then
    echo "FAIL: missing required marker: $1" >&2
    FAIL=1
  else
    echo "OK: $1"
  fi
}

check_absent "waiting for download"
check_absent "quad_psram"
check_present "display->begin OK"
check_present "Display canvas initialized."

if [[ "$FAIL" -eq 0 ]]; then
  echo ""
  echo "Boot verification PASSED."
  exit 0
fi

echo ""
echo "Boot verification FAILED."
exit 1
