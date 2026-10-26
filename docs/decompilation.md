---
title: Full decompilation verified against the recompiled program
tags: [decomp, differential, verification, recomp, readability, ledger]
status: verified (mechanism; Balloon Challenge show_thinking, 2026-10-24)
sources:
  - https://raw.githubusercontent.com/mozvip/cm-recomp/main/RECOMPILATION.md
  - https://github.com/abedegno/Exhume
  - https://github.com/cdlewis/snowboardkids2-decomp/blob/main/CLAUDE.md
---

> Background document from the porting knowledge base this port was built with. Paths there map to this
> repository as follows: `recomp/*.py` is `recomp/recompiler/`, `recomp/runtime/` is `recomp/runtime/`,
> `scripts/dbx_debug.py` and the DOSBox-X patch are in `tools/dosbox/`. Other paths refer to that knowledge base.

# Differential decompilation (readable code that provably matches)

Once the static recompilation passes lockstep (workflow/14), replace each recompiled game function with
readable C++. Each replacement is verified against the recompiled function it replaces. This is cm-recomp's
`RC_VERIFY`: it stops at the first difference.

## Loop per function (smallest first, main module last)
1. Inputs: REA decompile/instructions, string/literal map, data-segment map, names already known.
2. Write readable C++ with named globals (a typed view over the data segment) that calls the same runtime services.
3. Differential check:
   - run both versions from the same snapshot, over every recorded session's call sites plus fuzzed states;
   - compare all memory writes, return registers and runtime calls (with arguments).
4. On equality, mark the ledger `verified`. Otherwise explain the difference, change one thing, and repeat.
5. Stop at 100% `verified`. The recompiled version stays available as a fallback (`--recomp`).

## Rules
- The recompiled function is the authority, not a spec or a guess.
- A replacement may never call back into translated code it is supposed to replace.
- Record every name with its evidence (literal xref, data use, runtime call pattern).

## Mechanism (claude-knowledge/recomp/runtime)
- `native.h`: a decompiled procedure registers its entry (`rc::NativeReg`) and is entered exactly where the
  recompiled one would be (return address on the stack). It calls translated code (runtime routines,
  other procedures) with `rt_call_far` / `rt_call_near`, which push the same return address the original
  CALL pushes and run until execution returns there.
- Cycle-exact: the C++ must charge the instruction count of the path the original takes, or CPU-paced
  behaviour drifts (workflow/14 timing, cookbook/dosboxx-cycle-exact-timing.md). For compiled BASIC this
  falls out of the compiler's fixed statement patterns: `qb.h` helpers (`call`, `set`, `inc`, `eq`, `ge`,
  `ret_near`) perform one pattern each with its count, so the code reads like the BASIC source.
- `MachineOptions::Native::Verify` (`rc_headless --verify-native`, or `REA_PORT_ARGS=--verify-native` for
  `lockstep.py port`): at every call the runtime forks; the child runs the recompiled procedure to its
  return and sends registers, instruction count, 1 MB + HMA, video planes and its port-write log; the
  parent runs the C++ and compares. The first difference stops the run and is named. Then the whole
  session is still compared against the DOSBox-X golden.
- Pitfalls met: an IF pattern's `jmp` counted twice (the instruction count caught it); the forked child
  inherited the previous call's port log (clear per call).
- Every original block start must be announced (`q.at`, branch helpers take their block addresses, call
  returns are automatic): timers and IRQs are serviced there exactly as in the recompiled code. Without it
  interrupt handlers (a music engine) run at different instructions and the port-write log differs.
- Breakpoints inside native procedures (key waits in runtime routines they call, internal stop addresses)
  use the synchronous `MachineOptions::on_break` handler; pokes at internal addresses fire at `q.at`.
  The forked verification side gets the same inputs (`on_break(primary = false)`: keys, clock, no output).
- Exception to "never run the code being replaced": inline 8087-emulator sequences (QuickBASIC INT 34h-3Dh)
  run through `rt_run(from, to)`; the emulator patches its own call sites (INT 3Dh executed twice) and
  picks the return address, so the flow belongs to the library.
- Debug method for an instruction-count difference: `RC_TRACE_BLOCKS` + `RC_TRACE_AT` (+ `--native`) log
  block starts with the clock from both versions (native announcements log the same lines); diff in order.
- A procedure no recorded session reaches is registered `verified = false` (drafted): Verify mode runs it,
  On mode does not.

## Scaling up: the pattern lifter (recomp/qblift.py)
For compiled BASIC most blocks are a few fixed statement patterns (argument pushes, calls, integer stores,
array-element address pushes). `qblift.py --proc SSSS:OOOO` emits the qb.h helper calls for every block it
recognises, with return addresses from discovery, and `// TODO` plus the instructions for the rest (loops,
expressions, IFs). Write helpers for the TODO shapes (e.g. a FOR-print loop), splice, add the BASIC meaning
in the header comment, then `--verify-native`. Balloon Challenge `directions` (1,350 instructions,
~200 blocks) passed on the first run this way.

## Next: code that never returns (QuickBASIC main body)
The main module body (Balloon Challenge: 0824:0030, ~2,800 blocks, GOTO-structured) is not a procedure, so
per-call verification does not apply. Plan:
1. Split it into regions at BASIC statement labels (jump targets): a region is an entry block plus the blocks
   reachable without passing another region entry.
2. Native region functions end by setting CS:IP to the exit block (a GOTO to another region) and return.
3. Region verification: fork at region entry; the child runs recompiled code until CS:IP is outside the
   region (or a call returns into another region), sends its state; the parent runs the native region and
   compares as for procedures.
4. Lift with qblift.py; the remaining shapes (jcc/jmp control flow, 8087 sequences via fpu_seq, array
   expressions) get helpers as they recur.
Whole-session lockstep against the DOSBox-X golden stays the final judge.
