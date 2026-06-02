#!/usr/bin/env bash
# Compile or flash/probe ESP32-S3 USB host rail diagnostic builds.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="compile"
PORT=""
CASE_FILTER=""
PROBE_DURATION=75
VERIFY_BOOT=1
RUN_PROBE=1
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${LOG_DIR:-$ROOT/diagnostics/$STAMP}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/run-hardware-diagnostics.sh [options]

Options:
  --compile-only          Compile each diagnostic build without flashing (default)
  --flash                 Flash each selected build over USB
  --port PATH             Serial port, for example /dev/cu.usbmodem1101
  --case NAME             Run one case: no-rails, sel-only, rails-no-sel,
                          vbus-only, limit-only, boost-only, normal
  --probe-duration SECS   BLE probe duration after flash (default: 75)
  --no-probe              Skip BLE probe after flashing
  --no-verify             Skip boot marker capture after flashing
  --log-dir DIR           Override diagnostics log directory

The script writes logs under diagnostics/<timestamp>/ by default. That directory
is intentionally gitignored.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --compile-only)
      MODE="compile"
      shift
      ;;
    --flash)
      MODE="flash"
      shift
      ;;
    --port)
      PORT="${2:?missing port}"
      shift 2
      ;;
    --case)
      CASE_FILTER="${2:?missing case name}"
      shift 2
      ;;
    --probe-duration)
      PROBE_DURATION="${2:?missing duration}"
      shift 2
      ;;
    --no-probe)
      RUN_PROBE=0
      shift
      ;;
    --no-verify)
      VERIFY_BOOT=0
      shift
      ;;
    --log-dir)
      LOG_DIR="${2:?missing log dir}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/summary.txt"

COMMON_DEFINES="ENABLE_BLE_DIAGNOSTICS=1 USB_HOST_DEFER_UNTIL_BLE_SUBSCRIBE_MS=30000 USB_HOST_START_AFTER_BLE_SUBSCRIBE_DELAY_MS=5000"

case_defines() {
  case "$1" in
    no-rails)
      echo "USB_HOST_ENABLE_POWER_RAILS=0"
      ;;
    sel-only)
      echo "USB_HOST_ENABLE_SEL=1 USB_HOST_ENABLE_VBUS=0 USB_HOST_ENABLE_LIMIT=0 USB_HOST_ENABLE_BOOST=0"
      ;;
    rails-no-sel)
      echo "USB_HOST_ENABLE_SEL=0 USB_HOST_ENABLE_VBUS=1 USB_HOST_ENABLE_LIMIT=1 USB_HOST_ENABLE_BOOST=1"
      ;;
    vbus-only)
      echo "USB_HOST_ENABLE_SEL=0 USB_HOST_ENABLE_VBUS=1 USB_HOST_ENABLE_LIMIT=0 USB_HOST_ENABLE_BOOST=0"
      ;;
    limit-only)
      echo "USB_HOST_ENABLE_SEL=0 USB_HOST_ENABLE_VBUS=0 USB_HOST_ENABLE_LIMIT=1 USB_HOST_ENABLE_BOOST=0"
      ;;
    boost-only)
      echo "USB_HOST_ENABLE_SEL=0 USB_HOST_ENABLE_VBUS=0 USB_HOST_ENABLE_LIMIT=0 USB_HOST_ENABLE_BOOST=1"
      ;;
    normal)
      echo "USB_HOST_ENABLE_POWER_RAILS=1"
      ;;
    *)
      echo "Unknown case: $1" >&2
      exit 1
      ;;
  esac
}

run_case() {
  local name="$1"
  local defines="$COMMON_DEFINES $(case_defines "$name")"
  local prefix="$LOG_DIR/$name"
  local flash_args=()

  if [[ -n "$PORT" ]]; then
    flash_args+=("$PORT")
  fi

  {
    echo "case=$name"
    echo "mode=$MODE"
    echo "defines=$defines"
    echo "started=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } | tee "$prefix.meta.txt"

  if [[ "$MODE" == "compile" ]]; then
    echo "[$name] compile-only" | tee -a "$SUMMARY"
    COMPILE_ONLY=1 BUILD_DEFINES="$defines" \
      "$ROOT/scripts/flash-bridge-s3.sh" "${flash_args[@]+"${flash_args[@]}"}" \
      2>&1 | tee "$prefix.compile.log"
    echo "[$name] compile OK" | tee -a "$SUMMARY"
    return
  fi

  echo "[$name] flash" | tee -a "$SUMMARY"
  BUILD_DEFINES="$defines" \
    "$ROOT/scripts/flash-bridge-s3.sh" "${flash_args[@]+"${flash_args[@]}"}" \
    2>&1 | tee "$prefix.flash.log"
  echo "[$name] flash OK" | tee -a "$SUMMARY"

  if [[ "$VERIFY_BOOT" == "1" ]]; then
    echo "[$name] verify boot" | tee -a "$SUMMARY"
    if CAPTURE_SECS=12 "$ROOT/scripts/verify-boot.sh" 2>&1 | tee "$prefix.boot.log"; then
      echo "[$name] boot markers OK" | tee -a "$SUMMARY"
    else
      echo "[$name] boot markers FAILED" | tee -a "$SUMMARY"
    fi
  fi

  if [[ "$RUN_PROBE" == "1" ]]; then
    echo "[$name] BLE probe ${PROBE_DURATION}s" | tee -a "$SUMMARY"
    if "$ROOT/scripts/probe-ble-midi.sh" --duration "$PROBE_DURATION" 2>&1 | tee "$prefix.ble.log"; then
      echo "[$name] BLE probe complete" | tee -a "$SUMMARY"
    else
      echo "[$name] BLE probe FAILED" | tee -a "$SUMMARY"
    fi
  fi

  "$ROOT/scripts/analyze-hardware-diagnostics.py" "$LOG_DIR" | grep "^$name:" | tee -a "$SUMMARY" || true
}

CASES=(no-rails sel-only rails-no-sel vbus-only limit-only boost-only normal)
if [[ -n "$CASE_FILTER" ]]; then
  case_defines "$CASE_FILTER" >/dev/null
  CASES=("$CASE_FILTER")
fi

{
  echo "Hardware diagnostics started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "mode=$MODE"
  echo "log_dir=$LOG_DIR"
  echo "cases=${CASES[*]}"
  echo ""
} | tee "$SUMMARY"

for case_name in "${CASES[@]}"; do
  run_case "$case_name"
done

echo "" | tee -a "$SUMMARY"
echo "Hardware diagnostics complete: $LOG_DIR" | tee -a "$SUMMARY"
