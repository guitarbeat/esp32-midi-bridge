#!/usr/bin/env python3
"""Read ESP32-S3 USB Serial/JTAG logs. Close this before flashing — exclusive port access."""

import fcntl
import serial
import sys
import time

port = "/dev/cu.usbmodem11101"
baud = 115200


def port_is_locked(path: str) -> bool:
    try:
        with open(path, "rb", buffering=0) as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(handle, fcntl.LOCK_UN)
        return False
    except OSError:
        return True


print(f"Listening on {port} ({baud} baud)")
print("Close this script before arduino-cli upload or esptool flash.")
print("Press Ctrl+C to exit.\n")

while True:
    if port_is_locked(port):
        print(f"{port} is busy (upload or another monitor?). Waiting...")
        time.sleep(1)
        continue

    try:
        with serial.Serial(port, baud, timeout=1) as ser:
            print("Connected.")
            while True:
                line = ser.readline()
                if line:
                    print(line.decode("utf-8", errors="replace").strip())
    except serial.SerialException as exc:
        print(f"Serial error: {exc}. Retrying...")
        time.sleep(0.5)
    except KeyboardInterrupt:
        break
