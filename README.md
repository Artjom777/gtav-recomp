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

## Windows release (clean room)

The Windows release (GitHub Releases) ships no Rockstar Games or Microsoft game code or data:
`gtav_launcher.exe` takes the player's own discs and Title Update 27, checks them by hash, runs
codegen on the player's executable, compiles it into `gtav_game.dll` with the bundled LLVM and
translates the game's shaders on first play. How it is built and checked, and how to do the same
on Linux: `docs/RELEASE_CLEANROOM.md`.

- `loader/`: the launcher (`build_launcher.cmd`)
- `gtav/fs/`: freestanding header and CRT the game DLL is built with (no MSVC or Windows SDK headers)
- `tools/game_dll/`: interface generation, reference hashes, dev build of the DLL
- `tools/release/`: `make_release.py` assembles the release, `scan_release.cmd` taint-scans it

## License

GPL-3.0-or-later (see `LICENSE`). Bundled third-party components keep their own licenses (the
release carries them in `LICENSES\`). Grand Theft Auto V is a trademark of Take-Two Interactive;
this project is not affiliated with Rockstar Games, Take-Two or Microsoft.
