#!/usr/bin/env python3
"""Pattern lifter for QuickBASIC 4.x compiled code: turns the compiler's statement patterns in one procedure
into qb.h helper calls (native procedures, workflow/15), with block addresses and return points from
discovery. Blocks it does not recognise are emitted as `// TODO` with their instructions, for hand work.
The output is a draft: it must pass `--verify-native` before it counts.

    python3 qblift.py EXE --ref-mem mem_entry.bin --entries entries.txt --proc 0824:CCB9 > draft.cpp
"""
import argparse
import sys
from pathlib import Path

import re

from iced_x86 import Formatter, FormatterSyntax, Mnemonic as M, OpKind, Register

from x86emit import emit, Unsupported

sys.path.insert(0, str(Path(__file__).resolve().parent))
from recompile import program_blocks  # noqa: E402

FMT = Formatter(FormatterSyntax.NASM)


def text(ins):
    return FMT.format(ins)


def imm(ins, i=1):
    return ins.immediate(i) & 0xFFFF


def lift_block(b, nxt):
    """Statements for one block (list of C++ lines), or None if not recognised."""
    ins = list(b.insns)
    out = []
    i = 0
    ret = "0x%04X" % nxt if nxt is not None else "?"

    def args_at(j):
        vals = []
        while j + 1 < len(ins):
            t0, t1 = text(ins[j]), text(ins[j + 1])
            if t1 != "push ax":
                break
            if t0 == "xor ax,ax":
                vals.append("0")
            elif t0.startswith("mov ax,") and ins[j].op1_kind == OpKind.IMMEDIATE16:
                vals.append("0x%X" % imm(ins[j]))
            else:
                break
            j += 2
        return vals, j

    while i < len(ins):
        t = text(ins[i])
        if t.startswith("mov word [") and ins[i].op1_kind == OpKind.IMMEDIATE16:
            out.append("q.set(0x%X, 0x%X);" % (ins[i].memory_displacement & 0xFFFF, imm(ins[i])))
            i += 1
            continue
        if t.startswith("add word [") and ins[i].op1_kind in (OpKind.IMMEDIATE8TO16, OpKind.IMMEDIATE16):
            return None
        if t == "push ax":
            out.append("q.push_ax();")
            i += 1
            continue
        if t.startswith("push word ["):
            out.append("q.push_mem(0x%X);" % (ins[i].memory_displacement & 0xFFFF))
            i += 1
            continue
        if t == "mov si,0F10h" or (t.startswith("mov si,") and ins[i].op1_kind == OpKind.IMMEDIATE16):
            # element address: mov si,desc ; mov bx,off ; add bx,[si+0Ah] ; push bx
            if i + 3 < len(ins) and text(ins[i + 1]).startswith("mov bx,") and ins[i + 1].op1_kind == OpKind.IMMEDIATE16 and text(ins[i + 2]) == "add bx,[si+0Ah]" and text(ins[i + 3]) == "push bx":
                out.append("q.push_elem_addr(0x%X, 0x%X, true);" % (imm(ins[i]), imm(ins[i + 1])))
                i += 4
                continue
            return None
        if t.startswith("mov bx,") and ins[i].op1_kind == OpKind.IMMEDIATE16 and i + 2 < len(ins) and text(ins[i + 1]) == "add bx,[si+0Ah]" and text(ins[i + 2]) == "push bx":
            out.append("q.push_elem_addr(c.si, 0x%X, false);" % imm(ins[i]))
            i += 3
            continue
        vals, j = args_at(i)
        if vals:
            for v in vals:
                out.append("q.push_imm(%s);" % v)
            i = j
            continue
        if ins[i].mnemonic == M.CALL and i == len(ins) - 1:
            if ins[i].op0_kind == OpKind.FAR_BRANCH16:
                out.append("q.callf(0x%04X, 0x%04X, %s);" % (ins[i].far_branch_selector, ins[i].far_branch16, ret))
            else:
                out.append("q.call_near(0x%04X, %s);" % (ins[i].near_branch16, ret))
            i += 1
            continue
        return None
    return out


def plain_ds(ins) -> bool:
    """A memory operand [disp16] in DS (no base, index or segment override)."""
    return ins.memory_base == Register.NONE and ins.memory_index == Register.NONE and ins.segment_prefix == Register.NONE


def statements(ins, cs):
    """C++ statements for a run of non-control instructions: qb.h helpers where a compiler pattern matches,
    else the exact instruction semantics (x86emit)."""
    out, i = [], 0
    while i < len(ins):
        t = text(ins[i])
        nxt = text(ins[i + 1]) if i + 1 < len(ins) else ""
        if nxt == "push ax" and (t == "xor ax,ax" or (t.startswith("mov ax,") and ins[i].op1_kind == OpKind.IMMEDIATE16)):
            out.append("q.push_imm(%s);" % ("0" if t == "xor ax,ax" else "0x%X" % imm(ins[i])))
            i += 2
            continue
        if t.startswith("mov word [") and ins[i].op1_kind == OpKind.IMMEDIATE16 and plain_ds(ins[i]):
            out.append("q.set(0x%X, 0x%X);" % (ins[i].memory_displacement & 0xFFFF, imm(ins[i])))
            i += 1
            continue
        if t.startswith("push word [") and plain_ds(ins[i]):
            out.append("q.push_mem(0x%X);" % (ins[i].memory_displacement & 0xFFFF))
            i += 1
            continue
        if t == "push ax":
            out.append("q.push_ax();")
            i += 1
            continue
        out += [l for l in emit(ins[i], cs)]
        i += 1
    return out


JUMP = re.compile(r"c\.cs = c\.cs; c\.ip = 0x([0-9A-F]{4}); return;")


def region_code(blocks, cs, start, end, name, entry=None):
    """Native function for the code reachable from `entry` (default: start) without leaving [start, end)."""
    entry = start if entry is None else entry
    inside = lambda a: a[0] == cs and start <= a[1] < end
    body, work = set(), [(cs, entry)]
    while work:
        x = work.pop()
        if x in body or x not in blocks or not inside(x):
            continue
        body.add(x)
        b = blocks[x]
        work.extend(b.succ[1:] if b.kind == "call" else b.succ)
    preds = {}
    for a, b in blocks.items():
        for s_ in (b.succ[1:] if b.kind == "call" else b.succ):
            preds.setdefault(s_, set()).add(a)
    others = sorted(x[1] for x in body if x[1] != entry and any(not inside(p) or p not in body for p in preds.get(x, ())))
    order = sorted(body, key=lambda v: v[1])

    def go(t):
        return "goto A_%04X;" % t if inside((cs, t)) and (cs, t) in body else "c.ip = 0x%04X; return;" % t

    L = ["void %s(Cpu& c) {" % name, "    Qb q{c};", "    goto L_%04X;  // the dispatcher has announced the entry block" % entry]
    for x in order:
        b = blocks[x]
        L.append("A_%04X: q.at(0x%04X);" % (x[1], x[1]))
        L.append("L_%04X:" % x[1])
        ins = list(b.insns)
        if b.kind == "int":
            cont = b.succ[0][1]
            L.append("    q.fpu_seq(0x%04X, 0x%04X);" % (x[1], cont))
            L.append("    " + (("goto L_%04X;" % cont) if inside((cs, cont)) and (cs, cont) in body else "c.ip = 0x%04X; return;" % cont))
            continue
        term = ins[-1] if b.kind in ("call", "jmp", "jcc", "ret") else None
        try:
            st = statements(ins[:-1] if term is not None else ins, cs)
        except Unsupported as e:
            st = ['// UNSUPPORTED %s' % e]
        L += ["    " + l for l in st]
        if b.kind == "call":
            ret = b.succ[1][1]
            if term.op0_kind == OpKind.FAR_BRANCH16:
                L.append("    q.callf_x(0x%04X, 0x%04X, 0x%04X);" % (term.far_branch_selector, term.far_branch16, ret))
            else:
                L.append("    q.call_near_x(0x%04X, 0x%04X);" % (term.near_branch16, ret))
            L.append("    " + (("goto A_%04X;" % ret) if inside((cs, ret)) and (cs, ret) in body else "c.ip = 0x%04X; return;" % ret))
        elif b.kind in ("jmp", "jcc"):
            for l in emit(term, cs):
                l2 = JUMP.sub(lambda m: go(int(m.group(1), 16)), l)
                L.append("    " + l2)
        elif b.kind == "ret":
            L += ["    " + l for l in emit(term, cs)]   # leaves the region (pops the return address)
        elif b.kind == "fall":
            n = (ins[-1].ip + ins[-1].len) & 0xFFFF
            L.append("    " + go(n))
        else:
            L.append('    // TODO terminator %s' % b.kind)
    L.append("}")
    return L, others


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--load-seg", type=lambda v: int(v, 0), default=0x0824)
    ap.add_argument("--ref-mem", required=True)
    ap.add_argument("--entries")
    ap.add_argument("--proc", required=True, help="SSSS:OOOO (procedure) or SSSS:OOOO-OOOO (region)")
    ap.add_argument("--name", default="region")
    a = ap.parse_args()
    if "-" in a.proc:
        seg, rng = a.proc.split(":")
        rng, _, ent = rng.partition("@")
        lo, hi = (int(v, 16) for v in rng.split("-"))
        _, blocks = program_blocks(a.exe, a.load_seg, a.ref_mem, a.entries)
        code, others = region_code(blocks, int(seg, 16), lo, hi, a.name, int(ent, 16) if ent else None)
        print("\n".join(code))
        print("// other entries into the region (from outside): %s" % " ".join("%04X" % o for o in others))
        return 0
    s, o = a.proc.split(":")
    entry = (int(s, 16), int(o, 16))
    _, blocks = program_blocks(a.exe, a.load_seg, a.ref_mem, a.entries)
    body, work = set(), [entry]
    while work:
        x = work.pop()
        if x in body or x not in blocks:
            continue
        body.add(x)
        b = blocks[x]
        work.extend(b.succ[1:] if b.kind == "call" else b.succ)
    for x in sorted(body, key=lambda v: v[1]):
        b = blocks[x]
        nxt = b.succ[1][1] if b.kind == "call" and len(b.succ) > 1 else None
        lines = lift_block(b, nxt) if b.kind in ("call", "fall") else None
        print("    // block %04X (%s)" % (x[1], b.kind))
        if lines is None:
            print("    // TODO")
            for ins in b.insns:
                print("    //   %04X  %s" % (ins.ip, text(ins)))
        else:
            for l in lines:
                print("    " + l)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
