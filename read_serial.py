#!/usr/bin/env python3
"""Read ESP32-S3 USB Serial/JTAG logs. Close this before flashing."""

import glob
import os
import shutil
import subprocess
import sys
import termios
import time
from functools import partial

print = partial(print, flush=True)

try:
    import serial
    from serial.serialutil import SerialException
except ImportError:
    print("Install pyserial: pip3 install pyserial")
    sys.exit(1)

BAUD = 115200
DOWNLOAD_MARKERS = ("waiting for download", "DOWNLOAD(USB/UART0)")


def find_port() -> str | None:
    for pattern in ("/dev/cu.usbmodem*", "/dev/tty.usbmodem*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


def find_esptool() -> str | None:
    home = glob.glob(
        f"{os.path.expanduser('~')}/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool"
    )
    return sorted(home)[-1] if home else shutil.which("esptool")


def open_port(path: str) -> serial.Serial:
    ser = serial.Serial()
    ser.port = path
    ser.baudrate = BAUD
    ser.timeout = 0.5
    # Keep DTR/RTS low so we do not accidentally enter download mode.
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except (OSError, termios.error) as exc:
        raise SerialException(f"could not open port {path}: {exc}") from exc
    return ser


def port_holder_pids(path: str) -> list[int]:
    try:
        out = subprocess.check_output(["lsof", "-t", path], stderr=subprocess.DEVNULL, text=True)
    except subprocess.CalledProcessError:
        return []
    return [int(line) for line in out.splitlines() if line.strip()]


def reset_app(port: str) -> None:
    esptool = find_esptool()
    if not esptool:
        print("esptool not found — press RESET on the board manually.")
        return
    print(f"Resetting app via esptool on {port}...")
    subprocess.run(
        [esptool, "--chip", "esp32s3", "--port", port, "--before", "no-reset", "--after", "watchdog-reset", "run"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(2)


def warn_download_mode(text: str) -> None:
    if not any(marker in text for marker in DOWNLOAD_MARKERS):
        return
    print(
        "\n*** DOWNLOAD MODE — firmware is NOT running ***\n"
        "  • Press RESET once (do not hold BOOT)\n"
        "  • Or re-flash: ./scripts/flash-bridge-s3.sh\n"
        "  • Or retry with: python3 read_serial.py --reset\n"
    )


def wait_for_port(timeout_s: float = 30.0) -> str:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        port = find_port()
        if port:
            holders = [pid for pid in port_holder_pids(port) if pid != os.getpid()]
            if not holders:
                return port
        time.sleep(0.5)
    print("No free /dev/cu.usbmodem* port found.")
    print("Plug in the board (Micro-USB / USB Serial/JTAG) and retry.")
    sys.exit(1)


def main() -> None:
    do_reset = "--reset" in sys.argv
    if do_reset:
        sys.argv.remove("--reset")

    port = wait_for_port()
    if do_reset:
        reset_app(port)
        port = wait_for_port()

    print(f"Listening on {port} ({BAUD} baud)")
    print("Close this script before arduino-cli upload or esptool flash.")
    print("Press Ctrl+C to exit.\n")

    saw_download = False
    while True:
        try:
            with open_port(port) as ser:
                print("Connected.")
                while True:
                    try:
                        line = ser.readline()
                    except (OSError, termios.error) as exc:
                        raise SerialException(f"serial device disconnected: {exc}") from exc
                    if not line:
                        continue
                    text = line.decode("utf-8", errors="replace").strip()
                    print(text)
                    if not saw_download and any(marker in text for marker in DOWNLOAD_MARKERS):
                        saw_download = True
                        warn_download_mode(text)
        except SerialException as exc:
            print(f"Serial error: {exc}. Retrying in 1s...")
            time.sleep(1)
            port = wait_for_port(timeout_s=15.0)
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
