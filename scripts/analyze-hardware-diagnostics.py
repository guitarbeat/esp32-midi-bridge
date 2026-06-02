#!/usr/bin/env python3
"""Summarize ignored hardware diagnostic logs.

Reads logs created by scripts/run-hardware-diagnostics.sh and prints one compact
line per case. This is intentionally heuristic: it helps triage BLE/USB evidence
without replacing direct log inspection.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


DIAG_RE = re.compile(
    r"DIAG USB stage=(?P<stage>.*?) rails=(?P<rails>SEL=\d VBUS=\d LIMIT=\d BOOST=\d) "
    r"seen=(?P<seen>\d) ready=(?P<ready>\d) canOut=(?P<can_out>\d) "
    r"vid=(?P<vid>[0-9A-Fa-f]{4}) pid=(?P<pid>[0-9A-Fa-f]{4}) .* "
    r"raw=(?P<raw>\d+) midi=(?P<midi>\d+) drops=(?P<drops>\d+)"
)


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(errors="replace")


def cases_for(log_dir: Path) -> list[str]:
    cases = set()
    for path in log_dir.glob("*.meta.txt"):
        cases.add(path.name.removesuffix(".meta.txt"))
    for path in log_dir.glob("*.ble.log"):
        cases.add(path.name.removesuffix(".ble.log"))
    return sorted(cases)


def summarize_case(log_dir: Path, case: str) -> str:
    meta = read_text(log_dir / f"{case}.meta.txt")
    boot = read_text(log_dir / f"{case}.boot.log")
    ble = read_text(log_dir / f"{case}.ble.log")
    flash = read_text(log_dir / f"{case}.flash.log")

    diag_matches = list(DIAG_RE.finditer(ble))
    last_diag = diag_matches[-1].groupdict() if diag_matches else {}
    notifications_match = re.search(r"Probe complete; notifications=(\d+) wrote=(\w+)", ble)
    notifications = int(notifications_match.group(1)) if notifications_match else 0
    wrote = notifications_match.group(2) if notifications_match else "unknown"

    disconnected = "Disconnected:" in ble
    connected = "Connected" in ble
    subscribed = "Notify subscribed=true" in ble
    flashed = "flash OK" in read_text(log_dir / "summary.txt") or "Hash of data verified" in flash
    boot_ok = "[LCD] display->begin OK" in boot and "[SYSTEM] Display canvas initialized." in boot

    stage = last_diag.get("stage", "unknown").strip()
    rails = last_diag.get("rails", "unknown")
    seen = last_diag.get("seen", "unknown")
    ready = last_diag.get("ready", "unknown")
    can_out = last_diag.get("can_out", "unknown")
    vid_pid = f"{last_diag.get('vid', '0000')}:{last_diag.get('pid', '0000')}"
    raw = last_diag.get("raw", "0")
    midi = last_diag.get("midi", "0")
    drops = last_diag.get("drops", "0")

    status = "PASS"
    reasons: list[str] = []
    if not flashed:
        status = "WARN"
        reasons.append("no-flash-evidence")
    if boot and not boot_ok:
        status = "WARN"
        reasons.append("boot-marker-missing")
    if ble:
        if not connected or not subscribed:
            status = "WARN"
            reasons.append("ble-not-subscribed")
        if disconnected:
            status = "FAIL"
            reasons.append("ble-disconnected")
        if notifications == 0:
            status = "WARN" if status == "PASS" else status
            reasons.append("no-notifications")
    else:
        reasons.append("no-ble-log")

    defines = ""
    for line in meta.splitlines():
        if line.startswith("defines="):
            defines = line.removesuffix("\n").removeprefix("defines=")
            break

    reason_text = ",".join(reasons) if reasons else "ok"
    return (
        f"{case}: {status} {reason_text}; notifications={notifications} wrote={wrote}; "
        f"stage='{stage}' rails='{rails}' seen={seen} ready={ready} canOut={can_out} "
        f"vidpid={vid_pid} raw={raw} midi={midi} drops={drops}; defines='{defines}'"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_dir", type=Path, help="diagnostics/<timestamp> directory")
    args = parser.parse_args()

    log_dir = args.log_dir
    if not log_dir.is_dir():
        parser.error(f"not a directory: {log_dir}")

    case_names = cases_for(log_dir)
    if not case_names:
        parser.error(f"no diagnostic case logs found in {log_dir}")

    print(f"Hardware diagnostic summary: {log_dir}")
    for case in case_names:
        print(summarize_case(log_dir, case))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
