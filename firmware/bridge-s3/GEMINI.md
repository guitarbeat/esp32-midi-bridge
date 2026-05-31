# bridge-s3: Specific Instructions

This directory contains the primary firmware for ESP32-S3 boards.

## Structure

- `bridge-s3.ino`: The main entry point. It should remain a **Shallow Coordinator**, delegating all heavy lifting to deep modules.
- `BridgeUi.cpp/h`: Responsible for all display rendering and button handling. **Goal**: Move all rendering logic from the `.ino` here.
- `USBConnection.cpp/h`: Manages the USB MIDI Host stack.
- `BLEConnection.cpp/h`: Manages the Bluetooth LE MIDI Peripheral stack.
- `MidiBridge.cpp/h`: Coordinates MIDI routing.
- `animation/`: Contains the Bongo Cat animation engine.

## Guidelines

- **UI Deepening**: The `BridgeUi` should own the `Arduino_GFX` instance and the frame loop. The `.ino` should only call `bridgeUi.refresh()` and notify it of events.
- **Memory**: The ESP32-S3-USB-OTG has limited RAM for the display canvas (240x240 @ 16bpp = 115KB). Be mindful of heap usage.
- **Tasks**: MIDI processing and Connectivity should run in a dedicated FreeRTOS task to prevent UI lag.
