# Roland F-20 USB MIDI diagnostics

The Roland F-20 MIDI Implementation document (`F-20_MI.pdf`, Model F-20,
Version 1.00) describes MIDI message behavior, not USB descriptors. Use it after
USB enumeration works; it cannot explain a `No USB MIDI interface` display state
by itself.

## What the F-20 should transmit

Once a MIDI transport is established, the F-20 is expected to transmit standard
MIDI 1.0 messages that the bridge already parses:

- Note Off: `8n kk vv`
- Note On: `9n kk vv`
- Bank Select: CC0 and CC32
- Hold pedal: CC64
- Sostenuto: CC66
- Soft pedal: CC67
- Reverb send: CC91
- Program Change: `Cn pp`
- System Exclusive Identity Reply

The implementation chart lists transmitted note numbers as `15-113`. It also
lists System Real Time clock/commands as transmitted; the bridge filters MIDI
Clock by default unless built with `ENABLE_MIDI_CLOCK_PASSTHROUGH=1`.

## What the PDF does not provide

The PDF does not include:

- USB device descriptors
- USB class/subclass/protocol values
- USB endpoint layout
- A USB driver mode procedure

If the display says `USB WAIT` or `No USB MIDI interface`, the failure is below
the MIDI-message layer. Focus on USB host power, cable, Roland port, class
compliance, and descriptor capture.

## Debug order

1. Connect the Roland **USB COMPUTER** port directly to a Mac with the same cable.
2. Open **Audio MIDI Setup -> MIDI Studio** and confirm the F-20 appears.
3. If it does not appear on the Mac, fix the Roland port/cable/piano USB mode
   before changing firmware.
4. If it appears on the Mac but the ESP32 shows `No USB MIDI interface`, capture
   the F-20 USB descriptors and adapt `USBConnection` endpoint matching.
5. If the ESP32 display shows `USB MIDI`, press keys and pedals and verify Note
   On/Off plus CC64 increment bridge counters.

## Display diagnostics

- `USB WAIT`: the Roland is not enumerating.
- `USB NOMID`: a USB device or endpoint path is present, but no MIDI packets are
  arriving.
- `USB RAW`: USB packets are arriving, but MIDI decoding is dropping them.
- `USB MIDI`: decoded F-20 MIDI is reaching the bridge.
