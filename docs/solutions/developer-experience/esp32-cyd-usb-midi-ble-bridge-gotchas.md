---
title: ESP32-S3 USB-MIDI-BLE bridge development gotchas
date: 2026-05-29
category: developer-experience
module: firmware/bridge-s3
problem_type: developer_experience
component: tooling
severity: medium
applies_when:
  - "Extending firmware/bridge-s3 on ESP32-S3-USB-OTG with 240x240 ST7789"
  - "Blitting LVGL RGB565A8 sprites without LVGL (e.g. Bongo Cat from vostoklabs/bongo_cat_monitor)"
  - "Adding .cpp modules under Arduino sketch subfolders"
  - "Mixing ESP-IDF BLE APIs with the Arduino BLEDevice wrapper"
tags:
  - esp32-s3
  - st7789
  - lvgl-rgb565a8
  - bongo-cat
  - arduino-duplicate-symbol
  - usb-host-reconnect
  - ble-midi
---

# ESP32-S3 USB-MIDI-BLE bridge development gotchas

## Context

While extending `firmware/bridge-s3/` (Bongo Cat sprites, NVS settings, soft USB reconnect, link-health UI), four non-obvious embedded pitfalls caused garbled display, full board reboots on keyboard unplug, linker failures, and BLE stack conflicts. All were verified fixed with a clean `arduino-cli compile` for the ESP32-S3 target.

Prior session work (session history) tried an interleaved RGB565A8 blitter, `ESP.restart()` on USB disconnect, `#include` of root `.cpp` files from an aggregator module, and a custom `esp_ble_gap_register_callback()` for RSSI — each failed for the reasons below.

## Guidance

### 1. LVGL `RGB565A8` is two planes, not interleaved pixels

Sprites exported from LVGL as `LV_IMG_CF_RGB565A8` (e.g. [vostoklabs/bongo_cat_monitor](https://github.com/vostoklabs/bongo_cat_monitor)) store:

1. **RGB565 plane** — `w × h × 2` bytes, row-major
2. **Alpha plane** — `w × h` bytes, row-major, starting at offset `w × h × 2`

For 64×64: 8192 + 4096 = 12288 bytes (`data_size`).

**Wrong:** treat each row as `w × 3` bytes with alpha trailing the RGB in the same row.

**Correct:** read color and alpha separately:

```cpp
// firmware/bridge-s3/animation/BongoSprite.cpp
const uint32_t rgbRowBase = static_cast<uint32_t>(row) * w * 2;
const uint16_t color = readRgb565(data, rgbRowBase + static_cast<uint32_t>(col) * 2);

const uint32_t rgbPlaneSize = static_cast<uint32_t>(w) * h * 2;
const uint32_t alphaIndex = rgbPlaneSize + static_cast<uint32_t>(row) * w + col;
const uint8_t alpha = pgm_read_byte(&data[alphaIndex]);
```

Sanity-check: `data_size == w * h * 3`. Decode one known pixel before wiring animation.

### 2. USB unplug: tear down the device, not the whole board

`USB_HOST_CLIENT_EVENT_DEV_GONE` previously called `ESP.restart()`, which dropped the active BLE session and forced app reconnect.

**Prefer** `handleDeviceRemoved()`:

- Free IN/OUT transfers
- `usb_host_interface_release()` using a stored `midiInterfaceNumber` from claim time
- `usb_host_device_close()` and null `deviceHandle`
- Clear the MIDI queue and `deviceName`
- Call `onDeviceDisconnected()` for UI refresh

USB host install, client registration, and the core-0 USB task stay alive; replugging triggers normal `NEW_DEV` enumeration. BLE advertising/connection should remain up.

### 3. Arduino linking: never `#include` root-level `.cpp` files

Arduino auto-compiles every `.cpp` in the sketch root once. Subfolder `.cpp` files are **not** compiled unless pulled in (e.g. via `bongo_cat_module.cpp`).

**Rule:** `#include` headers only for root modules. Including `BridgeSettings.cpp` from `bongo_cat_module.cpp` while Arduino also compiles `BridgeSettings.cpp` causes duplicate symbol link errors.

```cpp
// bongo_cat_module.cpp — OK for subfolder sources only
#include "bongo_cat/BongoSprite.cpp"
#include "bongo_cat/BongoSprites.cpp"
#include "bongo_cat/BongoCat.cpp"
// Do NOT: #include "BridgeSettings.cpp"
```

On `multiple definition of` errors, grep for `#include ".*\.cpp"`.

### 4. Do not register a custom GAP callback on Arduino BLE

Adding `esp_ble_gap_register_callback()` plus `esp_ble_gap_read_rssi()` for RSSI conflicted with the Arduino `BLEDevice` wrapper (compile failures and risk of breaking connect/advertise).

**Prefer** app-layer metrics that use existing APIs only — e.g. USB→BLE forward latency via `recordForwardLatency()` and EMA in `BLEConnection`. RSSI needs a stack-safe hook or a pure ESP-IDF build, not both wrappers at once.

## Why This Matters

- **RGB565A8:** Wrong layout produces colorful noise; easy to misread as ST7789 init or scaling bugs.
- **USB reconnect:** Full reboot is a poor UX for a BLE bridge; users expect the iPad session to survive keyboard hot-plug.
- **Duplicate symbols:** Common when adding NVS/settings modules alongside sprite aggregators.
- **BLE GAP:** Mixing IDF callbacks with Arduino BLE is a frequent source of subtle production failures.

## When to Apply

- Importing new LVGL-exported sprite assets into `bongo_cat/`
- Changing USB disconnect / hot-plug behavior in `USBConnection.cpp`
- Adding new `.cpp` modules to the sketch (root vs subfolder)
- Adding BLE diagnostics (RSSI, MTU, connection params) on ESP32 Arduino 3.x

## Examples

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Bongo Cat is static/color noise | Interleaved RGB565A8 decode | Two-plane read in `BongoSprite.cpp` |
| Unplug keyboard → board reboots | `ESP.restart()` in `DEV_GONE` | `handleDeviceRemoved()` |
| Link error on `BridgeSettings::*` | `#include "BridgeSettings.cpp"` in aggregator | Header include only |
| BLE breaks after RSSI work | Custom `esp_ble_gap_register_callback` | Remove; use latency-only metrics |

## Related

- Root README: [Unplugging the Piano](../../../README.md) — BLE stays up on USB unplug
- `BUILD.md` — on-board controls and unplug behavior
- Ideation: [`docs/ideation/2026-05-29-github-repo-features-ideation.md`](../../ideation/2026-05-29-github-repo-features-ideation.md)
