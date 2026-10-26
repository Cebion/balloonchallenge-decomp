# Decisions

## Method: static recompilation, verified in lockstep; decompilation verified against it
- The goal is behaviour 1:1 with the DOS original. The whole program (game code and QuickBASIC runtime) is
  recompiled to C++ on a runtime that reproduces the reference PC (DOSBox-X: CPU timing, DOS/BIOS, EGA, PIT,
  PC speaker). Recorded play sessions are replayed in the original under an instrumented DOSBox-X and in the
  port; screens, game state, BIOS data and speaker output are compared at every key press.
- The decompilation into readable C++ (`src/game`) is checked call by call against the recompiled program
  (`--verify-native`: registers, flags, instruction count, memory, video planes, port writes).
- The default build runs the recompiled program; the decompiled procedures are opt-in (`--native`).
- An earlier engine written from a behaviour spec was retired: it could not be shown to match the original
  everywhere.

## Game files and the registered version
- No game files and no code generated from the game are in this repository; the build generates everything
  from the shareware v2.0 `BALLOONC.EXE` (checked by hash).
- The shareware release may be passed along unmodified under its own terms (NOTICE.md).
- The program always runs the recompiled shareware v2.0 code. Files of the registered version in the game
  folder switch it to registered mode (two human players, no shareware delays): the registration flag is set
  right after the program's own `BALLOONC.OV2` check. A registered `BALLOONC.OV2` also unlocks through the
  program's own check.

## Reference machine without game bytes
- `reference/mem_entry.bin` is the DOSBox-X machine at the program's entry with the program area blanked.
  The build reruns the EXE's own PKLITE/EXEPACK unpackers on it (Unicorn) and gets the capture back
  byte-identical: the unpackers' leftovers lie inside DGROUP and are part of the program's start state.

## License
- GPL-2.0-or-later for this project's code (the runtime follows DOSBox-X behaviour and the reference holds
  DOSBox-X BIOS/DOS memory). The game remains © William Soleau / Soleau Software (NOTICE.md).
