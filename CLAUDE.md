# Balloon Challenge port: notes for coding agents

Read `README.md` first. This repository is self-contained: everything needed to build is in it, except the
game files (the shareware `ballon20.zip`) and the build tools listed in the README.

## Build (Linux)
1. Install the tools: CMake >= 3.16, a C++17 compiler, SDL2 development files, Python 3 with `iced-x86`
   and `unicorn`, deark >= 1.7.
2. `scripts/prepare.sh /path/to/ballon20.zip` (or a folder with the game files). It checks the EXE hash,
   unpacks it, rebuilds the reference machine, generates `recomp/gen/` and the region/module sources and
   builds `recomp/build-rc/rc_play` and `rc_headless`.
3. After source changes: `recomp/regen.sh` (regenerates and rebuilds).

## Rules
- Never commit game files or anything generated from them: `original/`, `work/`, `recomp/gen/`,
  `recomp/build*/`, run folders, generated `src/game/region_*.cpp` / `module_*.cpp` (see `.gitignore`).
- The default build runs the recompiled program (decompiled procedures off). Behaviour must stay 1:1 with
  the original; do not change the runtime or generated code to "fix" game behaviour.
- If something fails to build on a platform, report the exact compiler error; do not add flags such as
  `-fsigned-char` without showing a signed-char problem in the code.

## Checks after a build
- `recomp/build-rc/rc_headless --data original --ms 15000 --png shot.png` writes the title screen.
- Full verification against DOSBox-X needs the instrumented DOSBox-X (`tools/dosbox/`); see README.
