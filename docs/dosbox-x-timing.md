---
title: Matching DOSBox-X's emulated time to the cycle (what the reference charges, and how to measure it)
tags: [dosbox-x, timing, lockstep, recomp, cycles, verification]
status: verified (Balloon Challenge, 2026-10-24)
---

> Background document from the porting knowledge base this port was built with. Paths there map to this
> repository as follows: `recomp/*.py` is `recomp/recompiler/`, `recomp/runtime/` is `recomp/runtime/`,
> `scripts/dbx_debug.py` and the DOSBox-X patch are in `tools/dosbox/`. Other paths refer to that knowledge base.

# Matching DOSBox-X's emulated time

CPU-paced code (busy loops, the moment a SOUND call is queued against notes already playing) only matches
the reference when the port charges what DOSBox-X charges. Settings: `core=normal`, `cycles=fixed 8000`,
`machine=svga_s3` (PCI bus on by default).

## What the reference charges (verified by instruction traces)
| Item | DOSBox-X behavior | Source |
|---|---|---|
| Instruction | 1 cycle, counted before it runs (`while (CPU_Cycles-->0)`) | core_normal.cpp |
| REP string op, N iterations | N + ceil(N/4): "interruptible rep string op" = -1 means chunks of 4, and each restart is another instruction | cpu.cpp, core_normal/string.h |
| String op without REP | 2 cycles (instruction + its single iteration) | core_normal/string.h |
| Port I/O | read: 8000 × ns / 1e6 cycles (8-bit 1020 ns, 16-bit 660 ns); write: 3/4 of that; not charged inside callbacks | iohandler.cpp |
| INT 21h AH=0B/2C/06 | `overhead()`: IP += 2 into the stub's `mov cx,140h; loop $` | dos.cpp |
| INT 21h AH=2C | reads the clock through INT 1Ah AH=0, run as real code (`CALLBACK_RunRealInt`); AL keeps the midnight flag, AH=2C | dos.cpp |
| INT 21h 4400h on CON | `GetInformation()` called twice, each runs INT 16h AH=11h as real code; handles 0-4 are all CON | dos_ioctl.cpp, dev_con.h |
| File I/O (3C/3D/3E/3F/40/41/42) | `diskio_delay`: bytes / 8333333 B/s (PCI default) of emulated time spent in the idle routine (12 NOPs + stop callback, interrupts on); open 1024, create 2048, close 512, seek 32, delete 1024, read/write = bytes moved | dos.cpp |
| Disk I/O | unmasks IRQ0 at the PIC (`disk_io_unmask_irq0`) | dos.cpp |
| Nested interrupts | trampoline in ROM, `INT n; FE 38 <stop>` × 256, 6 bytes each (F000:DA00 here) | callback.cpp |
| Display timing | frame grid fixed at the mode setup (`vga_mode_time_base`), period 1000/fps; 3DA retrace and blanking windows from the CRTC (mode 10h: retrace 2 lines = 0.064 ms) | vga_draw.cpp, vga_misc.cpp |
| BIOS tick phase at program start | arbitrary (~18 ms into the PIT0 period here); log it | timer.cpp |
| Debugger stop + F5 | about +2 cycles per stop (instruction counted at the break, then stepped with `CPU_Cycles = oldCycles-1`) | debug.cpp |
| PIC event slice cut | about 1 cycle lost each time the event queue ends a slice (VGA scanline events ~every 31.8 µs in mode 10h, timer events): about 0.3% slower than the instruction count | pic.cpp, core loop |

## How to measure (instrumentation in scripts/dosbox-x-rea-instrumentation.patch)
- `REA_LOG_BREAK`: CS:IP, emulated ms and PIT0 time base at every debugger stop (entry time, tick phase).
- `REA_LOG_VGA`: display setups (`D` lines with all delays) and frame starts (`F`).
- `REA_LOG_CB`: every callback (number, AX, ms, caller CS:IP): DOS/BIOS timeline. Port: `RC_TRACE_CB`.
- `REA_LOG_INSN` + `REA_LOG_INSN_AT` + `REA_LOG_INSN_MS`: CS:IP and ms of each instruction for a window after
  an address is first reached. Port: `RC_TRACE_BLOCKS` + `RC_TRACE_AT` + `RC_TRACE_MS` (block starts).
- Align: walk the port's block starts and find the same address in the reference's trace; print where the
  difference in cycles steps. Steps every N iterations of a loop point at an instruction class; steady +1
  steps at a fixed period point at a scheduler effect.
- Always compare a port run against the reference run it was fed from (each golden run has its own clock,
  so its own board and timings).
