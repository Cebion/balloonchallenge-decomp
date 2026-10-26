#!/usr/bin/env python3
"""Procedures of a recompiled program: call targets inside a code range, with their bodies.

    python3 procedures.py EXE --load-seg 0x0824 --ref-mem mem_entry.bin --entries entries.txt \\
        --range 0824:0000-0824:DB20 [--json out.json]

A procedure is the target of a near or far CALL. Its body is every block reachable from the entry along
jumps, branches, fall-through, interrupts and callbacks, and past calls to their return point, without
entering callees; it ends at RET/RETF. Bodies of different procedures may share blocks (QuickBASIC GOSUB
targets jumped into, shared epilogues); each is listed with its own count.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from recompile import program_blocks  # noqa: E402


def parse_addr(a: str):
    s, o = a.split(":")
    return int(s, 16), int(o, 16)


def procedures(blocks, lo, hi):
    lin = lambda a: (a[0] << 4) + a[1]
    callers = {}
    for addr, b in blocks.items():
        if b.kind == "call" and b.succ:
            t = b.succ[0]
            if lin(lo) <= lin(t) < lin(hi):
                callers.setdefault(t, []).append(addr)
    out = []
    for entry in sorted(callers, key=lin):
        body, work = set(), [entry]
        while work:
            a = work.pop()
            if a in body or a not in blocks:
                continue
            body.add(a)
            b = blocks[a]
            succ = b.succ[1:] if b.kind == "call" else b.succ
            work.extend(succ)
        ninsn = sum(len(blocks[a].insns) for a in body)
        out.append({"entry": "%04X:%04X" % entry, "blocks": len(body), "insns": ninsn,
                    "callers": len(callers[entry]), "far": any(blocks[c].insns[-1].op0_kind != blocks[c].insns[-1].op0_kind or
                                                               (blocks[c].succ[0][0] != c[0]) for c in callers[entry])})
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--load-seg", type=lambda v: int(v, 0), default=0x0824)
    ap.add_argument("--ref-mem", required=True)
    ap.add_argument("--entries")
    ap.add_argument("--range", required=True, help="SSSS:OOOO-SSSS:OOOO")
    ap.add_argument("--json")
    a = ap.parse_args()
    lo, hi = (parse_addr(x) for x in a.range.split("-"))
    _, blocks = program_blocks(a.exe, a.load_seg, a.ref_mem, a.entries)
    procs = procedures(blocks, lo, hi)
    if a.json:
        Path(a.json).write_text(json.dumps(procs, indent=1))
    for p in procs:
        print(f"{p['entry']} blocks {p['blocks']:4d} insns {p['insns']:5d} callers {p['callers']:3d} {'far' if p['far'] else 'near'}")
    print(f"{len(procs)} procedures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
