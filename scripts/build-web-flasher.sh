#!/usr/bin/env bash
# Build the ESP32-S3 firmware and refresh GitHub Pages web-flasher assets.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FQBN='esp32:esp32:esp32s3usbotg:PartitionScheme=default_8MB,USBMode=hwcdc'
BUILD_PATH="${BUILD_PATH:-$ROOT/firmware/bridge-s3/build/arduino-cli}"
SITE_DIR="${SITE_DIR:-$ROOT/site}"
VERSION="${VERSION:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local)}"

mkdir -p "$BUILD_PATH" "$SITE_DIR/firmware"
rm -f "$ROOT/firmware/bridge-s3/LocalBuildConfig.h"

echo "Compiling bridge-s3 production firmware..."
arduino-cli compile \
  --build-path "$BUILD_PATH" \
  --fqbn "$FQBN" \
  "$ROOT/firmware/bridge-s3"

ESPTOOL=""
for dir in "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py" "$HOME/.arduino15/packages/esp32/tools/esptool_py"; do
  if [[ -d "$dir" ]]; then
    ESPTOOL="$(find "$dir" -name esptool -type f 2>/dev/null | sort -V | tail -1)"
  fi
done

BOOT_APP0=""
for dir in "$HOME/Library/Arduino15/packages/esp32/hardware/esp32" "$HOME/.arduino15/packages/esp32/hardware/esp32"; do
  if [[ -d "$dir" ]]; then
    BOOT_APP0="$(find "$dir" -path '*/tools/partitions/boot_app0.bin' -type f 2>/dev/null | sort -V | tail -1)"
  fi
done

if [[ -z "$ESPTOOL" || -z "$BOOT_APP0" ]]; then
  echo "Could not locate esptool or boot_app0.bin. Install esp32:esp32 core." >&2
  exit 1
fi

echo "Merging web-flasher binary..."
"$ESPTOOL" --chip esp32s3 merge-bin \
  -o "$SITE_DIR/firmware/bridge-s3-esp32s3-usb-otg.bin" \
  --pad-to-size 8MB \
  --flash-mode keep \
  --flash-freq keep \
  --flash-size keep \
  0x0 "$BUILD_PATH/bridge-s3.ino.bootloader.bin" \
  0x8000 "$BUILD_PATH/bridge-s3.ino.partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD_PATH/bridge-s3.ino.bin"

cat > "$SITE_DIR/manifest.json" <<EOF
{
  "name": "Piano BLE Bridge",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "improv": false,
      "parts": [
        {
          "path": "firmware/bridge-s3-esp32s3-usb-otg.bin",
          "offset": 0
        }
      ]
    }
  ]
}
EOF

python3 - "$SITE_DIR" <<'PY'
import json
import sys
from pathlib import Path

site = Path(sys.argv[1])
manifest = json.loads((site / "manifest.json").read_text())
assert manifest["name"] == "Piano BLE Bridge"
build = manifest["builds"][0]
assert build["chipFamily"] == "ESP32-S3"
part = build["parts"][0]
assert part["offset"] == 0
firmware = site / part["path"]
assert firmware.is_file(), firmware
assert firmware.stat().st_size == 8 * 1024 * 1024, firmware.stat().st_size
assert (site / "index.html").is_file()
PY

echo "Web flasher ready: $SITE_DIR"
