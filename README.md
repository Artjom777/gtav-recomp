# GTA V (Xbox 360, TU27) static recompilation

Native build of GTA V's Xbox 360 executable (Linux first, other platforms planned), statically recompiled with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) v0.10.0. No game files or generated code
are in this repo; you need your own copy of the game.

- `gtav/`: project (CMake, manifest of extra entry points, `src/` hooks and fixes)
- `rexglue-sdk-gtav-linux.patch`: all changes on top of rexglue-sdk v0.10.0
- `play.sh`: launcher; `hangdump.sh`: stack dump of a running game
- `scripts/`, `tools/`: manifest derivation, XEX/STFS extraction, script disassembly helpers
- `NEXT_SESSION_PLAN.md`: current status, fixes, open issues

Status (2026-10-24): plays through the prologue and beyond. Next: a native renderer.
