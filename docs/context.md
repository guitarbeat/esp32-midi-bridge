# Architecture Language

Use these terms exactly when discussing or implementing changes. This ensures consistency between agents and human developers.

## Core concepts

- **Module** — A unit of code with a defined **Interface** and **Implementation**. Could be a class (e.g., `BridgeUi`), a file, or a group of files.
- **Interface** — Everything a caller must know to use a module correctly: public methods, types, invariants, ordering constraints, error modes, and required configuration. Not just the type signature.
- **Implementation** — The internal logic and private state hidden behind the interface.
- **Depth** — Leverage at the interface: a lot of behaviour behind a small interface. A **deep** module hides significant work behind a simple call. A **shallow** module requires the caller to manage its internals.
- **Seam** — An interface that allows behaviour to be swapped or mocked (e.g., for testing). One adapter is a hypothetical seam; two adapters make it a real seam.
- **Adapter** — A concrete implementation of an interface at a seam.
- **Locality** — Related data and behaviour kept in one place to reduce cognitive load and bugs.
- **Leverage** — What callers get from depth: more capability per unit of interface they must learn.

## Principles

- **Depth is a property of the interface, not the implementation.** A deep module can be internally composed of small, mockable parts — they just aren't part of the interface.
- **The deletion test.** Imagine deleting the module. If complexity vanishes, it was a pass-through. If complexity reappears across N callers, it was earning its keep.
- **The interface is the test surface.** Callers and tests cross the same seam.
- **One adapter means a hypothetical seam. Two adapters means a real one.** Don't introduce a seam unless something actually varies across it.

## Domain specifics

- **MidiBridge** — The central coordinator that routes MIDI packets between transports. Calls `MidiEngine::prepareOutbound` once per accepted packet before broadcasting; channel filter and transpose gate outbound together.
- **prepareOutbound** — MidiEngine entry for routing: applies filter/transpose, updates UI state, returns false when the packet must not leave the bridge.
- **MidiMidiTransport** — A physical or logical MIDI connection (USB, BLE, RTP-MIDI, UART-MIDI).
- **MidiMidiMidiTransportKind** — Stable route-stat category for a MidiMidiTransport: USB host, BLE, RTP-MIDI, or UART-MIDI.
- **canSend** — MidiMidiTransport capability query used by MidiBridge before routing to an output. USB host returns true only when a USB MIDI OUT endpoint exists.
- **BridgeUi** — The module responsible for all visual feedback and user input handling.
- **NetworkMidiMidiTransport** — A deep module that manages all network-related state (WiFi, Provisioning, RTP-MIDI).

## Rejected framings

- **Depth as ratio of implementation-lines to interface-lines**: rewards padding the implementation. Use depth-as-leverage instead.
- **"Interface" as only public methods**: too narrow — includes every fact a caller must know.
- **"Boundary"**: overloaded with DDD's bounded context. Say **seam** or **interface**.
