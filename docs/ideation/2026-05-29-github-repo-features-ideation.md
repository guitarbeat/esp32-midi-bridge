---
date: 2026-05-29
topic: github-repo-features
focus: look into GitHub repositories and see what else we can add
mode: repo-grounded
---

# Ideation: Features From Related GitHub Projects

## Grounding Context

**Codebase context:** Single-purpose **Piano BLE Bridge** (`firmware/bridge-s3/`) — ESP32-S3 USB host IN → BLE MIDI, 240×240 ST7789 UI, Bongo Cat animation, `BridgeUi` (mini keyboard, velocity, sustain, transport status, backlight dim), CI `.bin` artifacts, browser flasher, and classic ESP32+MAX3421E fallback. It uses a focused `Transport` architecture rather than porting the full `sauloverissimo/ESP32_Host_MIDI` hub.

**External context (GitHub):**

| Repo | Relevance |
|------|-----------|
| [MorrisMakes/MIDI-to-BT](https://github.com/MorrisMakes/MIDI-to-BT) | Same product shape: S3 USB host → **BLE + RTP-MIDI simultaneously**, TFT status, last event, **Active Sense filter**, GPIO Wi-Fi toggle |
| [sauloverissimo/ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) | Upstream inspiration; **WiFi/RTP, USB device, DIN, ESP-NOW, OSC** — scope intentionally avoided here |
| [vostoklabs/bongo_cat_monitor](https://github.com/vostoklabs/bongo_cat_monitor) | Full LVGL cat + **web flasher** + desktop typing companion; we integrated sprites only |
| [xloc/esp-midi-adapter](https://github.com/xloc/esp-midi-adapter) | Minimal S3 Atom bridge (ESP-IDF); pairing UX note only |
| [guitarbeat/esp32-cyd-midi-ble-bridge](https://github.com/guitarbeat/esp32-cyd-midi-ble-bridge) | This repo (~1★); niche but focused |

**Past learnings:** Durable troubleshooting notes now live under `docs/solutions/`.

## Topic Axes

1. **Wireless transports** — BLE-only today vs dual BLE+WiFi, message filtering
2. **Setup & persistence** — compile-time flags vs NVS/runtime config, OTA
3. **Display & feedback** — cat fidelity, stats, device identity, link health
4. **MIDI processing** — filters, transpose, clock, panic variants
5. **USB host reliability** — reconnect, VBUS, enumeration feedback

## Ranked Ideas

### 1. Optional WiFi RTP-MIDI (Apple MIDI) alongside BLE

**Description:** Add a compile-time (then optionally NVS) path to mirror USB MIDI to **RTP-MIDI on port 5004** while BLE stays up — same pattern as MorrisMakes/MIDI-to-BT. Lets a Mac use Network MIDI in Logic/GarageBand while an iPad stays on BLE.

**Axis:** Wireless transports

**Basis:** `external:` [MorrisMakes/MIDI-to-BT README_ESP32_MIDI_Bridge.md](https://github.com/MorrisMakes/MIDI-to-BT/blob/main/README_ESP32_MIDI_Bridge.md) documents simultaneous BLE + RTP from one USB host stream.

**Rationale:** Closest peer implementation to this product; fills the largest protocol gap without adopting the full ESP32_Host_MIDI hub.

**Downsides:** Wi-Fi stack RAM/flash cost; static IP / provisioning UX; user must join same LAN.

**Confidence:** 85%

**Complexity:** High

**Status:** Partial (compile-time `ENABLE_RTP_MIDI` + AppleMIDI; NVS Wi-Fi credentials deferred)

---

### 2. Filter Active Sense (0xFE) before BLE notify

**Description:** Drop Active Sense keepalives in the USB→BLE path so wireless links are not flooded with useless packets (still allow on USB if bidirectional path enabled).

**Axis:** MIDI processing

**Basis:** `external:` MorrisMakes bridge explicitly filters 0xFE for both BLE and RTP outputs.

**Rationale:** Low-risk, measurable reduction in BLE traffic and skipped counters on some pianos.

**Downsides:** Rare gear that misuses Active Sense for timing (uncommon on digital pianos).

**Confidence:** 90%

**Complexity:** Low

**Status:** Shipped (USB queue + `buildBleMidiPacket`)

---

### 3. Show connected USB instrument name on the display

**Description:** Read USB string descriptors during enumeration and show manufacturer/product on the status panel (`deviceName` exists in `USBConnection` but is never populated).

**Axis:** Display & feedback

**Basis:** `direct:` `USBConnection.h` has `String deviceName` and README troubleshooting assumes users recognize *their* keyboard — field is unused in `USBConnection.cpp`.

**Rationale:** High UX value on 240×240 when multiple keyboards are tested; matches MorrisMakes “last event + identity” display philosophy.

**Downsides:** Descriptor parsing edge cases; long names need truncation.

**Confidence:** 80%

**Complexity:** Medium

**Status:** Shipped (`loadDeviceName` + dashboard)

---

### 4. Soft USB reconnect without full `ESP.restart()`

**Description:** On USB disconnect, re-init host stack and BLE advertising without rebooting; show “reconnect BLE in app” only when needed.

**Axis:** USB host reliability

**Basis:** `direct:` README documents intentional `ESP.restart()` on unplug and user pain (“reconnect your app over Bluetooth”).

**Rationale:** Differentiator vs naive bridges; aligns with “hobbyist product” polish.

**Downsides:** ESP-IDF USB host state bugs may force keeping restart as fallback; more test matrix.

**Confidence:** 70%

**Complexity:** High

**Status:** Shipped (`handleDeviceRemoved` — BLE stays up)

---

### 5. NVS runtime settings (BLE name, transpose, channel, backlight timeout)

**Description:** Persist settings in NVS; change BLE name and musical options without reflash. Optional: captive portal or **BLE MIDI SYSEX/config characteristic** for phone-side edits (lighter than MorrisMakes hard-coded Wi-Fi in source).

**Axis:** Setup & persistence

**Basis:** `external:` Thibak/PCh uses USB mass-storage `.cfg` editing; MorrisMakes hard-codes network in firmware; this repo already documents compile-time `BLE_DEVICE_NAME_TEXT` only (`docs/build.md`).

**Rationale:** Reduces support friction; pairs with CI artifacts for non-developers.

**Downsides:** Flash wear minimal but schema versioning needed; scope creep if portal added.

**Confidence:** 75%

**Complexity:** Medium

**Status:** Shipped (`BridgeSettings` + BOOT multi-tap)

---

### 6. Link health: BLE RSSI + USB→BLE latency on screen

**Description:** Sample RSSI when connected; track timestamp delta from USB packet to BLE notify; show rolling avg on Full display mode.

**Axis:** Display & feedback

**Basis:** `direct:` README calls out “BLE MIDI latency depends on receiving device”; no measurement exists in firmware.

**Rationale:** Helps users distinguish bridge vs iPad/GarageBand issues; unique vs MorrisMakes “last event” only.

**Downsides:** RSSI API varies by core; latency noisy on loaded CPU.

**Confidence:** 65%

**Complexity:** Medium

**Status:** Partial (forward latency on Full display; RSSI deferred — conflicts with BLE stack callback)

---

### 7. Bongo Cat parity + “stage” display mode

**Description:** Import remaining upstream effects/sprites; add a display mode that hides metrics and maximizes cat (3× scale or centered 192px) for desk toy use — inspired by vostoklabs desk companion positioning without their desktop app.

**Axis:** Display & feedback

**Basis:** `external:` vostoklabs/bongo_cat_monitor ships full animation set + web flasher; `direct:` this repo integrated 15 sprites and 2× scale only.

**Rationale:** Emotional/product identity fit for repo name and existing UI investment.

**Downsides:** Flash size (~+200KB already seen); RAM for larger framebuffer.

**Confidence:** 80%

**Complexity:** Medium

**Status:** Partial (Stage mode shipped; full sprite set / 3× scale deferred for RAM)

---

### 8. USB MIDI 2.0 (UMP) host ingest

**Description:** Host native USB MIDI 2.0 devices and parse Universal MIDI Packets (UMP) directly on the ESP32-S3 USB host port. Negotiate Alt 1 (MIDI 2.0) when available, with transparent fallback to MIDI 1.0 Alt 0. Forward UMP to BLE/RTP outputs after down-scaling to MIDI 1.0 until BLE MIDI 2.0 transport exists.

**Axis:** Wireless transports / MIDI processing

**Basis:** `external:` [sauloverissimo/ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI) `USBMIDI2Connection` — UMP endpoint discovery, protocol negotiation, raw 32-bit word delivery. `external:` MIDI Association (Feb 2026) — BLE MIDI 2.0 transport still in development; no finalized spec for carrying UMP over Bluetooth.

**Rationale:** Forward-looking for next-gen keyboards that ship USB MIDI 2.0 natively. Keeps the bridge relevant when source devices upgrade beyond MIDI 1.0, even though current targets (Roland F-20) are 1.0-only.

**Downsides:** No BLE MIDI 2.0 transport yet — output must down-scale to BLE MIDI 1.0, so no end-to-end 2.0 benefit today. Significant USB host stack work (descriptor scan for Alt 0/1, UMP endpoint discovery, protocol negotiation). F-20 and most current pianos cannot use it. Large effort for marginal near-term gain.

**Confidence:** 60%

**Complexity:** High

**Status:** Roadmap (documented in README; not implemented)

---

## Rejection Summary

| # | Idea | Reason Rejected |
|---|------|-----------------|
| 1 | Port full ESP32_Host_MIDI hub | Subject-replacement; README explicitly single-purpose |
| 2 | GitHub Pages web flasher | Duplicate of prior product decision (CI artifact only) |
| 3 | Desktop companion (WPM/CPU typing) | Different product; requires host app (vostoklabs) |
| 4 | USB MIDI Device mode to PC | New transport/audience; not bridge-to-tablet story |
| 5 | DIN-5 / OSC / ESP-NOW | Hub scope from ESP32_Host_MIDI |
| 6 | Hard-coded Wi-Fi credentials in source | Anti-pattern from MorrisMakes; use NVS/portal instead |
| 7 | OTA-only without UX | High effort, low signal until RTP/NVS exist |
| 8 | MIDI channel remapping UI alone | Weaker than NVS bundle; defer until settings exist |
| 9 | CYD 2432 as primary target | Repo name vs product focus on S3-USB-OTG; keep fallback doc only |
| 10 | Multi-BLE central connections | BLE MIDI practical limit; little GitHub precedent |
