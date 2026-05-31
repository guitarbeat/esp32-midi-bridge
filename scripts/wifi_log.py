#!/usr/bin/env python3
"""Receive Wi-Fi UDP debug logs from the Piano BLE Bridge (port 3333)."""

import argparse
import socket
import sys


def main() -> None:
    parser = argparse.ArgumentParser(description="Listen for ESP32 Wi-Fi debug logs")
    parser.add_argument("--port", type=int, default=3333, help="UDP port (default 3333)")
    parser.add_argument("--bind", default="0.0.0.0", help="Bind address (default 0.0.0.0)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))

    print(f"Listening for Wi-Fi debug logs on {args.bind}:{args.port}")
    print("Enable on device: -DENABLE_WIFI_DEBUG=1 (board must be on Wi-Fi)")
    print("Press Ctrl+C to exit.\n")

    try:
        while True:
            data, addr = sock.recvfrom(4096)
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            if not text.endswith("\n"):
                sys.stdout.write("\n")
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
