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

Project-specific skills in `.agents/skills/`. Cursor and compatible agents discover these automatically.

| Skill | Use when |
|-------|----------|
| [esp32-firmware-engineer](skills/esp32-firmware-engineer/SKILL.md) | ESP32 hardware, peripherals, display, embedded debugging |
| [gui-ascii-visualizer](skills/gui-ascii-visualizer/SKILL.md) | Preview display layouts or explore UI architecture alternatives |
| [improve-codebase-architecture](skills/improve-codebase-architecture/SKILL.md) | Find deepening opportunities, refactor shallow modules |

## Local docs (not in git)

The `docs/` directory is gitignored but may exist locally:

- `docs/solutions/` — troubleshooting write-ups
- `docs/superpowers/specs/` — design specs
- `docs/adr/` — architecture decision records

When recording a new ADR, use `docs/adr/NNNN-short-title.md` with **Context**, **Decision**, and **Consequences** sections.
