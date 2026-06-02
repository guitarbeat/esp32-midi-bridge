#!/usr/bin/env bash
# Flash ESP32-S3 bridge firmware over ArduinoOTA once the board is on Wi-Fi.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN='esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc'
TARGET="${1:-esp32-midi-bridge.local}"
PASSWORD="${OTA_PASSWORD_TEXT:-}"

if [[ "$TARGET" == "--help" || "$TARGET" == "-h" ]]; then
  cat <<'EOF'
Usage:
  ./scripts/flash-bridge-s3-ota.sh [host-or-ip]

Examples:
  ./scripts/flash-bridge-s3-ota.sh
  ./scripts/flash-bridge-s3-ota.sh esp32-midi-bridge.local
  ./scripts/flash-bridge-s3-ota.sh piano-ble-bridge.local
  ./scripts/flash-bridge-s3-ota.sh 192.168.1.42

Set OTA_PASSWORD_TEXT in the environment if firmware was built with an OTA password:
  OTA_PASSWORD_TEXT='secret' ./scripts/flash-bridge-s3-ota.sh 192.168.1.42
EOF
  exit 0
fi

echo "OTA target: $TARGET"
echo "Compiling ($FQBN)..."
arduino-cli compile --fqbn "$FQBN" "$ROOT/firmware/bridge-s3"

UPLOAD_ARGS=(-p "$TARGET" --fqbn "$FQBN" "$ROOT/firmware/bridge-s3")
if [[ -n "$PASSWORD" ]]; then
  UPLOAD_ARGS=(--upload-property "upload.password=$PASSWORD" "${UPLOAD_ARGS[@]}")
fi

echo "Uploading over OTA..."
arduino-cli upload "${UPLOAD_ARGS[@]}"

echo ""
echo "OTA upload complete. The board should reboot into the new firmware."
