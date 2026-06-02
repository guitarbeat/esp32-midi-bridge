# Agent Resources

Everything an AI coding agent needs for this repo lives here and in the docs below.

## Entry points

| File | Role |
|------|------|
| [../README.md](../README.md) | User-facing project overview |
| [../docs/agent-instructions.md](../docs/agent-instructions.md) | **Start here** — role, architecture, build/verify, firmware scope, learned facts |
| [../docs/context.md](../docs/context.md) | Shared vocabulary (domain + architecture language) |
| [../docs/build.md](../docs/build.md) | Build, flash, recovery, Wi-Fi debug |

## Skills

Project-specific skills live in `.agents/skills/`. Keep agent skills here rather
than duplicating them in tool-specific folders; other agents should read this
index and `docs/agent-instructions.md`.

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
