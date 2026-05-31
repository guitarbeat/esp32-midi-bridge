---
name: gui-ascii-visualizer
description: Visualizes embedded GUIs using ASCII art and proposes architectural UI alternatives. Use when the user wants to preview a display layout or explore non-destructive UI improvements for ESP32/Arduino devices.
---

# GUI ASCII Visualizer

This skill specializes in low-fidelity visualization of embedded user interfaces and the exploration of "Deep Module" architectural alternatives for display logic.

## Workflow

### 1. Analysis
Read the current UI implementation (e.g., `BridgeUi.cpp`, `refresh()` methods) to identify:
- Screen dimensions (e.g., 240x240).
- Static elements (Sidebars, Headers).
- Dynamic elements (Progress bars, Terminal logs, Animations).
- Module coupling (Does the UI manage its own state or observe an engine?).

### 2. ASCII Visualization
Generate an ASCII representation of the current GUI. Use consistent symbols:
- `+---+` for borders and containers.
- `[---]` or `[|||]` for bars and chips.
- `> TEXT` for terminal/log output.
- `( ... )` for sprite placeholders (e.g., Bongo Cat).

### 3. Alternative Prototyping
Propose UI improvements focused on **Locality** and **Leverage**. For each alternative:
- Explain the **Architectural Benefit** (e.g., "Moves note tracking to a sub-module").
- Provide an **ASCII Mockup**.
- Assess **Resource Impact** (Heap usage, CPU cycles for refresh).

## Design Patterns

### The "Deep Viewer" Pattern
Refactor shallow UI modules into pure observers. The UI should only know how to render, not how to calculate state.
- **Before**: UI counts notes and manages timers.
- **After**: UI queries `MidiEngine::heldNotes()` and `Board::getUptime()`.

### Performance-First UI
- **Partial Refresh**: Only draw what changed.
- **Double Buffering**: Mention `Arduino_Canvas` usage in proposals.
- **ASCII Fidelity**: Ensure the ASCII mockup reflects realistic screen constraints (e.g., character width vs. pixel width).

## Examples

### Current MIDI Station Visualization
```text
+-------+------------------------------------------+
| TRANS |  PIANO BRIDGE                 [ 12:45 ]  |
|  +0   | ---------------------------------------- |
|       |  +----------+          +----------+      |
| FILTER|  | USB HOST |          | BLE MIDI |      |
|  ALL  |  | [ONLINE] |          | [ONLINE] |      |
|       |  +----------+          +----------+      |
| MODE  |                                          |
| PERF  |  ACTIVITY STREAM                         |
|       |  +------------------------------------+  |
| [---] |  | > ON  C4 (v84)                     |  |
| [---] |  | > OFF C4 (v0)                      |  |
| [---] |  | > ON  E4 (v72)             [ . ]      |
| [---] |  | > CC  64=127                       |  |
|       |  +------------------------------------+  |
| [BATT]|                                          |
| [||| ]|          ( BONGO CAT HERE )              |
+-------+------------------------------------------+
```
