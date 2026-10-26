# Decompilation ledger: Balloon Challenge

Status per code unit: `recomp-only` (runs as recompiled code), `drafted` (C++ written, not yet verified),
`verified` (C++ in src/game, every call in the listed sessions equal to the recompiled procedure under
`--verify-native`: registers, flags, instruction count, memory, video planes, port writes; and the run
still matches the DOSBox-X golden).

Scope (from `recomp/recompiler/procedures.py` and discovery):
- main module 0824: 19,695 instructions; 12 GOSUB procedures (below), the rest is the GOTO-structured
  main body, to be split at statement labels;
- separately compiled modules 15D6, 1664, 16CD, 1794, 17ED, 17FE, 1932, 1946, 195B: about 4,900
  instructions (font loading/printing, page flip, delays, key input; which are user code and which are
  library is still to be established);
- QuickBASIC runtime 19AE: stays recompiled (library, not game code).

## Main module GOSUB procedures
| entry | name | kind | insns | status |
|---|---|---|---|---|
| 0824:4FA1 | count_score | GOSUB | 215 | verified (71 calls: newgame, endgame, regsmoke) |
| 0824:521B | title_screen | GOSUB | 602 | verified (4 calls: newgame, options, regsmoke; both banners) |
| 0824:5880 | scan_balloons | GOSUB | 149 | verified (66 calls: newgame, endgame) |
| 0824:5A27 | show_thinking | GOSUB | 94 | verified (541 calls: newgame, endgame) |
| 0824:5B29 | hit_animation | GOSUB | 330 | verified (12 calls: endgame, newgame) |
| 0824:5F1F | confirm_quit | GOSUB | 320 | verified (1 call: newgame; Y path only) |
| 0824:667B | next_player | GOSUB | 301 | drafted (no session reaches it: needs a recording where a player runs out of balloons) |
| 0824:69F3 | eliminate_grounded | GOSUB | 373 | verified (1 call: reg2p_full) |
| 0824:762A | decide_first | GOSUB | 592 | verified (5 calls: newgame, endgame, regsmoke) |
| 0824:7D32 | select_opponent | GOSUB | 237 | verified (2 calls: regsmoke, reg2p; Human path only) |
| 0824:AD7B | read_registration | GOSUB | 151 | verified (every session; OV2-present path only) |
| 0824:CCB9 | directions | GOSUB | 1350 | verified (1 call: directions session; registered tail not exercised) |

## Main body and modules
| unit | insns | status |
|---|---|---|
| main body 0824 (outside the GOSUBs) | ~15,000 | recomp-only |
| modules 15D6-195B | ~4,900 | recomp-only |

## Rules learned
- Every original block start is announced in the C++ (`q.at`, branch helpers, call returns): timers and
  IRQs are serviced there as in the recompiled code, otherwise interrupt handlers (the music engine) run
  at other instructions (count_score failed on port-write timing until this was done).
- Breakpoints and pokes inside native code work through the same announcements and the synchronous
  `on_break` handler; the forked verification side gets the same inputs (`primary = false`).
- Paths not exercised by any session are not verified: listed per procedure.
- Inline 8087-emulator sequences (INT 34h-3Dh with the instructions around them) run as the original code
  (`q.fpu_seq`): the emulator patches its INT 3Dh call sites on first use and returns to them, so the
  control flow is the library's, not the procedure's (decide_first). The statement is still documented
  in BASIC (`RANDOMIZE TIMER: W = <RND expression>`); decoding the exact expression is open.

## Style note
Procedures with expressions (array reads, coordinates) are translated statement by statement: the BASIC
meaning is in the comments and helper names, the code keeps the compiled registers, flags, stack-frame
temporaries and instruction counts, because the verifier compares all of them (scan_balloons.cpp).
Simple call/assign/IF procedures read like BASIC through qb.h (show_thinking.cpp).
