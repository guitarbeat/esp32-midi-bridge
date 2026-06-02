# Agent Resources

Everything an AI coding agent needs for this repo lives here and in the root-level files below.

## Entry points

| File | Role |
|------|------|
| [../AGENTS.md](../AGENTS.md) | **Start here** — role, architecture, build/verify, firmware scope, learned facts |
| [../CONTEXT.md](../CONTEXT.md) | Shared vocabulary (domain + architecture language) |
| [../BUILD.md](../BUILD.md) | Build, flash, recovery, Wi-Fi debug |
| [../README.md](../README.md) | User-facing project overview |

## Skills

Project-specific skills live in `.agents/skills/`. Keep agent skills here rather
than duplicating them in tool-specific folders; other agents should read this
index and the root `AGENTS.md`.

| Skill | Use when |
|-------|----------|
| [esp32-firmware-engineer](skills/esp32-firmware-engineer/SKILL.md) | ESP32 hardware, peripherals, display, embedded debugging |
| [gui-ascii-visualizer](skills/gui-ascii-visualizer/SKILL.md) | Preview display layouts or explore UI architecture alternatives |
| [improve-codebase-architecture](skills/improve-codebase-architecture/SKILL.md) | Find deepening opportunities, refactor shallow modules |

## Documentation

Project documentation is tracked under `docs/`:

- `docs/solutions/` — troubleshooting write-ups
- `docs/superpowers/specs/` — design specs
- `docs/adr/` — architecture decision records

When recording a new ADR, use `docs/adr/NNNN-short-title.md` with **Context**, **Decision**, and **Consequences** sections.
