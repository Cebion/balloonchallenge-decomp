# Balloon Challenge port

An unofficial port of **Balloon Challenge v2.0** (William Soleau / Soleau Software, 1993-1995), a DOS strategy
game, to Linux with SDL2.

The port runs the original DOS program, translated to native code by a static recompiler, on a runtime that
reproduces the PC it ran on (EGA, PC speaker, timer, keyboard, DOS and BIOS). Its behaviour is verified to
be 1:1 with the original: recorded play sessions are replayed in the original under DOSBox-X and in the port,
and screens, game state and speaker output are compared at every key press.

This repository contains no game files and no code derived from the game's program; the build generates
everything from the shareware `BALLOONC.EXE`. See `NOTICE.md` for what belongs to whom.

## Requirements

- Linux, CMake 3.16 or newer, a C++17 compiler, SDL2 development files.
- Python 3 with `iced-x86` and `unicorn` (`pip install --user iced-x86 unicorn`).
- [deark](https://entropymine.com/deark/) 1.7 or newer (unpacks the PKLITE and EXEPACK layers of the EXE).
- The shareware release `ballon20.zip` (e.g. from classicdosgames.com),
  sha256 `99b71a805a759cdad2b7ed6121c1221934a5c3430d99223ee164055e10ae4fd0`.

## Build

```shell
scripts/prepare.sh /path/to/ballon20.zip
```

This copies the game files to `original/` (not tracked), checks that `BALLOONC.EXE` is the v2.0 shareware
program, unpacks it, rebuilds the reference machine from it, generates the recompiled program into
`recomp/gen/` and builds `recomp/build-rc/rc_play`. After changes, `recomp/regen.sh` regenerates and
rebuilds.

## Play

```shell
recomp/build-rc/rc_play --data original              # window
recomp/build-rc/rc_play --data original --fullscreen
```

The folder given with `--data` holds the game files; the game writes its own files there too. With the
files of the registered version in that folder the port runs in registered mode (two human players); a
registered `BALLOONC.OV2` also unlocks through the game's own check.

## Layout

| Path | Contents |
|---|---|
| `recomp/` | build of the recompiled port: `regen.sh`, `CMakeLists.txt`, `entries.txt` (code entry points found by discovery runs), `host/` (SDL2 player, headless runner, game-file check) |
| `recomp/recompiler/` | the static recompiler (Python, iced-x86): MZ loading, code discovery, x86-16 to C++ translation, reference-machine tools, QuickBASIC pattern lifter |
| `recomp/runtime/` | the C++ runtime the recompiled program runs on: CPU state, DOS and BIOS services, EGA, PIT/PIC, PC speaker, native-procedure support |
| `reference/` | the reference machine at the program's entry (DOSBox-X, program area blanked) and its display and timer timing logs |
| `src/game/` | decompiled procedures (C++, checked call by call against the recompiled program); generated region and module files are not tracked |
| `sessions/` | recorded play sessions (key sequences) used for verification |
| `tools/` | lockstep verification, region and module generators, coverage report |
| `docs/` | decisions, decompilation ledger |
| `docs/` | `recompilation.md`, `decompilation.md`, `dosbox-x-timing.md`: how the recompiler, the verified decompilation and the timing model work |

## Verification (development)

- `tools/verify_sessions.sh NAME...`: replays `sessions/play/NAME.json` in the original under an
  instrumented DOSBox-X (`tools/dosbox/dosbox-x-rea-instrumentation.patch`, driven by `tools/dosbox/dbx_debug.py`;
  set `REA_DOSBOX` to its binary) and in the port, then
  compares every stop.
- `recomp/record.sh NAME [--registered]`: record a new session by playing.
- `rc_headless --verify-native`: runs the decompiled C++ and checks each call against the recompiled program
  (registers, flags, instruction count, memory, video, port writes); `tools/native_coverage.py` reports which
  units the sessions reached.

Verification status: all 11 recorded sessions in `sessions/play/` match the original at every stop (screens,
game state, BIOS data); speaker output is identical in 9 of 11, the other two differ at one note join (a
DOSBox-X scheduling effect, `docs/dosbox-x-timing.md`).

## License

The port's code: GPL-2.0-or-later (`LICENSE`). The game is not covered; see `NOTICE.md`.
