---
title: Static recompilation of 16-bit DOS programs (1:1 port default)
tags: [recomp, static-recompilation, x86-16, dos, runtime, ega, pit, lockstep, fpu]
status: verified
sources:
  - https://github.com/mozvip/cm-recomp
  - https://raw.githubusercontent.com/mozvip/cm-recomp/main/RECOMPILATION.md
  - https://github.com/carlosrmz009/littleplanet
  - https://github.com/sp00nznet/xboxrecomp
  - https://github.com/abedegno/Exhume
---

> Background document from the porting knowledge base this port was built with. Paths there map to this
> repository as follows: `recomp/*.py` is `recomp/recompiler/`, `recomp/runtime/` is `recomp/runtime/`,
> `scripts/dbx_debug.py` and the DOSBox-X patch are in `tools/dosbox/`. Other paths refer to that knowledge base.

# Static recompilation (default route to a 1:1 port)

Translate every reachable instruction of the unpacked EXE (game code AND its compiler runtime) into C,
one C function per x86 function, over an exact CPU model (registers, segment registers, flags) and a
1 MB memory array. A small runtime provides DOS/BIOS/video/timer/keyboard/sound. No emulator loop, so
the result is behaviour-identical by construction. Proven on 16-bit DOS by cm-recomp (Championship Manager, SDL2).

## Pipeline (recomp/ in this KB)
1. **Input:** a fully unpacked MZ (all packer layers removed; check the relocation count). Load it at a fixed
   segment with relocations applied. Use the same load segment as the DOSBox-X reference so addresses compare.
2. **Decode:** 8086/186/286 + x87 decoder.
3. **Discover:**
   - recursive descent from the entry point, plus seeds (MZ relocations at `9A` far calls, near-call targets);
   - jump tables become `switch`;
   - functions reached only through pointers go into `entries.txt`.
   Discovery runs execute the port headless and log missing targets until none remain (cm-recomp `make discover`, `RC_MISSING`).
4. **Translate:** C per function over `struct CPU`, with exact flags (lazy only where proven identical), `INT n` into
   the runtime, and far call/ret preserving CS.
   - FPU: report "no coprocessor" so a compiler's 8087-emulator path runs (Microsoft INT 34h-3Dh, Borland
     variants): the emulator is part of the program's runtime library and is recompiled like the rest, which
     makes results bit-exact on x86-64 and aarch64 (Balloon Challenge). A program that needs real x87
     instructions needs a software 80-bit FPU (e.g. SoftFloat extF80) in the runtime; not built yet.
5. **Runtime:**
   - IVT in memory (games hook INT 8/9/1Ch), PIC/PIT, BIOS data area, INT 10h/16h/1Ah/21h;
   - EGA/VGA registers with planar memory and latches, palette, retrace bit;
   - speaker via PIT channel 2;
   - deterministic emulated cycle clock;
   - IRQs delivered at back-edges and calls.
6. **Self-modifying code:** keep the code static by choosing the runtime path (for example "no coprocessor" so FPU
   emulator sites are not patched). Trap any other write into code ranges and fall back to an interpreter.

## Verification (the judge)
- **Instruction conformance:** run the original bytes in Unicorn and the translated C on random states, then
  compare registers, flags and memory (xboxrecomp conformance, Exhume fuzzasm).
- **Lockstep sessions:** record the original in scripted DOSBox-X (cookbook/dosboxx-scripted-debugger.md):
  keys with emulated timestamps, timer/RTC values, checkpoint screens + data segment + speaker port writes.
  Replay in the port and require exact equality at every checkpoint.
- **Coverage ledger:** functions translated / reached; "no translated code for SSSS:OOOO" must never fire.

## Known pitfalls (from cm-recomp)
- Screens not reached in discovery runs miss pointer-only functions: drive sessions through EVERY screen.
- Run from a copy of the data directory; games write files (saves, registration state).

## Proven on Balloon Challenge (2026-10-18): recomp/ in this KB
Files: `recomp/mzload.py` (loader, verified 0-byte difference vs DOS), `discover.py`, `x86emit.py`,
`recompile.py` (blocks.cpp + image.cpp + ledger.json), `refmachine.py` (reference machine regions, vectors,
callback map), `make_bootenv.py`, `conformance.py` (Unicorn oracle), `runtime/` (cpu.h, machine.cpp, ega.cpp,
bootenv.h, machine_api.h). Project side: `regen.sh` (bootenv + translate + build), `lockstep_discover.sh`,
`tools/lockstep.py` (golden / port / compare).

Results and rules learned:
- **Instruction conformance:** 7,270 of 7,271 blocks equal to Unicorn over 43,622 random states. The single
  exception was a random self-overwrite.
  Semantics fixed by the oracle:
  - a word access at offset FFFFh is linear-contiguous (no wrap within the segment);
  - A20 is enabled (HMA reachable);
  - SAR beyond the operand width fills with the sign;
  - REP counts toward the instruction budget.
- **Pin the reference machine first** (DOSBox-X): `fpu=false`, `ems/xms/umb=false`, `int33=false`,
  `cputype=386`, `cycles=fixed 8000`, `quit warning=false`. The port emulates exactly that PC.
  With an FPU present the Microsoft runtime patches its emulator calls into real x87 code.
- **The reference machine, not a lookalike** (`refmachine.py`, `make_bootenv.py`, capture at the unpacked entry):
  all conventional memory (IVT, BDA, DOS data, MCBs, environment, PSP, and the unpacker residue above the
  image, which the program can read as uninitialised memory), video + system ROM, HMA, entry registers, PIC
  mask. The runtime loads exactly that, so vectors the program saves, the model byte at FFFF:000E, the BIOS
  date and far allocations all equal the reference.
- **Recompile the reference BIOS stubs too.** DOSBox-X handlers are x86 stubs around a 4-byte callback
  opcode `FE 38 nn nn` (INT 8: `sti; cb; push ds/ax/dx; int 1Ch; EOI; iret`; INT 9 reads port 60h and calls
  INT 15h/4Fh itself; INT 21h: `sti; cb; iret; retf; push cx; mov cx,140h; loop $; pop cx; iret`).
  Discovery treats the callback as a block end; the runtime runs callback nn as the native service of
  the vector whose stub contains it, with DOSBox-X semantics: flags written into the stacked IRET frame
  (CALLBACK_SCF/SZF), and IP adjustments (INT 16h wait: +1 into the NOP loop; INT 21h AH=0Bh/2Ch
  `overhead()`: +2 into the 320-iteration delay loop). Stub instruction counts then match the reference.
- **DOS get-time must use the reference formula** (DOSBox-X: hundredths = (100 / (1193182/65536)) x ticks as
  one double, then split). QuickBASIC TIMER (19AE:337C here) uses INT 21h AH=2Ch, and RANDOMIZE TIMER
  seeds RND from it: a different rounding gives a different board.

## Lockstep design that works (proven on Balloon Challenge)
- **Stops at code events, not at times.** Break at the runtime's blocking key wait (QB: the INKEY poll loop,
  19AE:59D3 here) and at the game's input call; inject the next key word into the BIOS buffer at every
  stop (`SM 0040:001E W:key`, head/tail 1E/20). A key at the first iteration ends the wait, so each wait is
  exactly one stop and both machines see the same stop sequence. Pre-buffer the title keys at the entry.
- **The clock is an input.** Wall-time alignment between DOSBox-X cycles and an instruction clock drifts
  (~5% here). Record every DOS get-time result in the reference (DOSBox-X patch
  `scripts/dosbox-x-rea-instrumentation.patch`, env `REA_LOG_DOSTIME`, logs caller CS:IP, CX, DX) and replay them in
  order in the port (`MachineOptions::dos_time_replay`; a different caller stops the run).
- **Mirror debugger side effects:** the plane dump writes GC 3CE=4/3CF=n; the port does the same at stops.
- **Compare** planes and the whole data segment per stop; mask only dead stack below SP (interrupts land
  on different instructions), and report the masked count.
- **Microsoft 8087 emulator:** the program's runtime contains it, so floating point is bit-exact by
  construction. INT 34h-3Bh continuations skip the emulated operand bytes. **INT 3Dh is self-modifying**: the
  handler rewrites `CD 3D` to `89 C0` (mov ax,ax) and re-executes. The translated INT 3Dh therefore checks the
  byte in memory.
- **Self-modifying code detection:** a bitmap of translated bytes. Any other write into code is logged, and
  executing modified code stops with an error. No silent stale code.
- **Dynamic discovery:** the runtime entered the QuickBASIC main module indirectly, which needed a seed for
  `load_seg:0000`. Then `discover_loop.sh` (run, add the missing target, re-translate, rebuild) found about 63
  entries for startup.
- **BIOS video calls made by the QuickBASIC runtime:** INT 10h 00, 01, 02, 03, 05, 06, 09 (graphics), 0F,
  1000h/1002h, 1130h, 1202h (BL=10h), 1Bh (BX=0), EFh. Each was implemented as DOSBox-X does it.

## Timing that the reference defines (proven on Balloon Challenge, 2026-10-23)
- **PIT count loading:** in modes 2/3 a count written without a new mode word loads at the end of the current
  period (8254 datasheet, DOSBox-X write_latch). QuickBASIC's background sound engine reprograms channel 0
  (divisor 0800h, ~582 Hz) per sound without a mode word; loading it at once made notes 30x too short.
- **I/O bus delay:** DOSBox-X charges each port access (iodelay=-1, ISA 8.33 MHz): 8-bit 8.5 bus clocks,
  16-bit 5.5, writes 3/4 of reads, not inside callbacks. Graphics-heavy games do enough I/O that leaving it
  out drifted the clock ~5%.
- **Speaker lockstep:** log raw writes (PIT ch2 data/mode, port 61h) on both sides (DOSBox-X patch:
  REA_LOG_SPEAKER) and require the identical sequence; compare 61h on its writable bits 0-3 (4-7 are
  read-only status bits echoed by read-modify-write) and drop the reference's BIOS POST writes. Report
  interval medians by size class: they show whether timing is tick-, retrace- or CPU-paced.
- **What has a true answer:** pitch (PIT crystal) and BIOS-tick/TIMER waits are identical on every PC;
  CPU-paced timing only has "the reference machine's" answer. Offer --speed/--refresh in the host.

## Distributable reference machine (packed programs)
The reference capture (DOSBox-X memory at the unpacked entry) contains the program and its unpackers'
leftovers, which the program may read (Balloon Challenge: they lie inside DGROUP; blanking them changed the
start state of every shareware session). To ship the reference without game bytes:
1. Blank `[load_seg*16, A0000)` in the capture and commit that (DOSBox-X BIOS/DOS memory only, GPL).
2. At build time `recomp/unpack_reference.py PACKED.EXE BLANK.bin OUT.bin --load-seg --entry` loads the
   packed EXE into it as DOS does and runs the EXE's own unpacker code under Unicorn to the entry. Balloon
   Challenge (PKLITE + EXEPACK): byte-identical to the capture, same entry registers.
3. `refmachine.apply()` is the single place that combines capture regions and the program image (the
   recompiler and the conformance oracle both use it; the capture's copy of the image may be blank).
