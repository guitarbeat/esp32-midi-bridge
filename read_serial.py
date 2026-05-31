#!/usr/bin/env python3
"""Read ESP32-S3 USB Serial/JTAG logs. Close this before flashing."""

import glob
import sys
import time

try:
    import serial
    from serial.serialutil import SerialException
except ImportError:
    print("Install pyserial: pip3 install pyserial")
    sys.exit(1)

BAUD = 115200


def find_port() -> str:
    for pattern in ("/dev/cu.usbmodem*", "/dev/tty.usbmodem*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return "/dev/cu.usbmodem11101"


def open_port(path: str) -> serial.Serial:
    ser = serial.Serial()
    ser.port = path
    ser.baudrate = BAUD
    ser.timeout = 0.5
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def main() -> None:
    port = find_port()
    print(f"Listening on {port} ({BAUD} baud)")
    print("Close this script before arduino-cli upload or esptool flash.")
    print("Press Ctrl+C to exit.\n")

    while True:
        try:
            with open_port(port) as ser:
                print("Connected.")
                while True:
                    line = ser.readline()
                    if line:
                        print(line.decode("utf-8", errors="replace").strip())
        except SerialException as exc:
            print(f"Serial error: {exc}. Retrying in 1s...")
            time.sleep(1)
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
