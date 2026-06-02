#!/usr/bin/env bash
# Probe the bridge BLE MIDI service from macOS.
set -euo pipefail

DURATION=30
SEND_NOTE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --duration)
      DURATION="${2:?missing duration}"
      shift 2
      ;;
    --send-note)
      SEND_NOTE=1
      shift
      ;;
    --help|-h)
      cat <<'EOF'
Usage:
  ./scripts/probe-ble-midi.sh [--duration seconds] [--send-note]

Subscribes to Piano-BLE-Bridge BLE MIDI notifications and prints received bytes.
With --send-note, writes two middle-C Note On/Off pairs to test BLE -> USB OUT.
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

swift - "$DURATION" "$SEND_NOTE" <<'SWIFT'
import Foundation
import CoreBluetooth

func out(_ text: String) {
    FileHandle.standardOutput.write((text + "\n").data(using: .utf8)!)
}

final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    let duration: TimeInterval
    let sendNote: Bool
    let serviceUUID = CBUUID(string: "03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
    let characteristicUUID = CBUUID(string: "7772E5DB-3868-4112-A1A9-F2669D106BF3")
    var central: CBCentralManager!
    var target: CBPeripheral?
    var notifications = 0
    var wrote = false

    init(duration: TimeInterval, sendNote: Bool) {
        self.duration = duration
        self.sendNote = sendNote
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        out("BLE state: \(central.state.rawValue)")
        guard central.state == .poweredOn else { return }
        let connected = central.retrieveConnectedPeripherals(withServices: [serviceUUID])
        for peripheral in connected {
            let name = peripheral.name ?? "(unnamed)"
            out("Already connected \(name) state=\(peripheral.state.rawValue)")
            attachAndConnect(peripheral, alreadyConnected: true)
            return
        }
        central.scanForPeripherals(withServices: [serviceUUID], options: nil)
    }

    func attachAndConnect(_ peripheral: CBPeripheral, alreadyConnected: Bool) {
        target = peripheral
        peripheral.delegate = self
        central.stopScan()

        if alreadyConnected || peripheral.state == .connected {
            out("Using connected peripheral state=\(peripheral.state.rawValue)")
            peripheral.discoverServices([serviceUUID])
        } else {
            out("Connecting state=\(peripheral.state.rawValue)")
            central.connect(peripheral, options: nil)
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let name = peripheral.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        out("Discovered \(name.isEmpty ? "(unnamed)" : name) RSSI=\(RSSI)")
        guard name == "Piano-BLE-Bridge" || name.contains("Piano") else { return }
        attachAndConnect(peripheral, alreadyConnected: false)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        out("Connected")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        out("Connect failed: \(error?.localizedDescription ?? "none")")
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        out("Disconnected: \(error?.localizedDescription ?? "none")")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            out("Service discovery failed: \(error.localizedDescription)")
            return
        }
        for service in peripheral.services ?? [] where service.uuid == serviceUUID {
            peripheral.discoverCharacteristics([characteristicUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error = error {
            out("Characteristic discovery failed: \(error.localizedDescription)")
            return
        }
        guard let characteristic = service.characteristics?.first(where: { $0.uuid == characteristicUUID }) else {
            out("BLE MIDI characteristic not found")
            return
        }
        out("Subscribing. Play notes now.")
        peripheral.setNotifyValue(true, for: characteristic)

        if sendNote {
            let noteOn = Data([0x80, 0x80, 0x90, 60, 100])
            let noteOff = Data([0x80, 0x81, 0x80, 60, 0])
            for _ in 0..<2 {
                peripheral.writeValue(noteOn, for: characteristic, type: .withoutResponse)
                RunLoop.current.run(until: Date().addingTimeInterval(0.35))
                peripheral.writeValue(noteOff, for: characteristic, type: .withoutResponse)
                RunLoop.current.run(until: Date().addingTimeInterval(0.35))
            }
            wrote = true
            out("Wrote BLE MIDI test notes")
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        out("Notify subscribed=\(characteristic.isNotifying) err=\(error?.localizedDescription ?? "none")")
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error = error {
            out("Notification error: \(error.localizedDescription)")
            return
        }
        guard let data = characteristic.value else { return }
        notifications += 1
        if let text = String(data: data, encoding: .utf8), text.hasPrefix("DIAG") {
            out(text)
        } else {
            let hex = data.map { String(format: "%02X", $0) }.joined(separator: " ")
            out("MIDI_NOTIFY #\(notifications) len=\(data.count) bytes=\(hex)")
        }
    }
}

let duration = TimeInterval(Double(CommandLine.arguments[1]) ?? 30)
let sendNote = CommandLine.arguments[2] == "1"
let probe = Probe(duration: duration, sendNote: sendNote)
RunLoop.current.run(until: Date().addingTimeInterval(duration))
if let target = probe.target {
    probe.central.cancelPeripheralConnection(target)
}
out("Probe complete; notifications=\(probe.notifications) wrote=\(probe.wrote)")
SWIFT
