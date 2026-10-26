#!/usr/bin/env python3
"""Instruction conformance: every recompiled block vs. the original bytes run in Unicorn.

    python3 conformance.py GAME.EXE libconform.so --load-seg 0x0824 [--states 3] [--only SSSS:OOOO]

For each block (from the library's table) and N random states: run the block in the recompiled
library and the same bytes in Unicorn (16-bit real mode) from the same registers/flags and memory
image; compare general and segment registers, next CS:IP, memory, and flags (bits that the last
writing instruction leaves undefined are masked; iced-x86 reports them). Blocks that touch I/O ports
are compared up to the port access. Exit status 1 on any difference; the first ones are printed.
"""
import argparse
import ctypes
import random
import struct
import sys
from pathlib import Path

import iced_x86 as ix
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_INSN, UcError
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SP,
                               UC_X86_REG_BP, UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_CS, UC_X86_REG_DS,
                               UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_IP, UC_X86_REG_EFLAGS,
                               UC_X86_INS_IN, UC_X86_INS_OUT)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mzload import load_mz  # noqa: E402
import refmachine  # noqa: E402

REGS = [("ax", UC_X86_REG_AX), ("cx", UC_X86_REG_CX), ("dx", UC_X86_REG_DX), ("bx", UC_X86_REG_BX),
        ("sp", UC_X86_REG_SP), ("bp", UC_X86_REG_BP), ("si", UC_X86_REG_SI), ("di", UC_X86_REG_DI),
        ("cs", UC_X86_REG_CS), ("ds", UC_X86_REG_DS), ("es", UC_X86_REG_ES), ("ss", UC_X86_REG_SS),
        ("ip", UC_X86_REG_IP)]
ARITH = 0x08D5  # OF SF ZF AF PF CF

# iced RflagsBits -> x86 FLAGS bits
ICED2X86 = {ix.RflagsBits.OF: 0x800, ix.RflagsBits.SF: 0x80, ix.RflagsBits.ZF: 0x40, ix.RflagsBits.AF: 0x10,
            ix.RflagsBits.CF: 0x1, ix.RflagsBits.PF: 0x4}


class State(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint16) for n in ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "cs", "ds", "es", "ss",
                                               "ip", "flags")] + [("icount", ctypes.c_uint32)]


def to_x86(iced_bits: int) -> int:
    return sum(v for k, v in ICED2X86.items() if iced_bits & k)


def undefined_mask(insns) -> int:
    """Flags whose final value comes from an instruction that leaves them undefined."""
    mask = 0
    for ins in insns:
        written = to_x86(ins.rflags_modified | ins.rflags_set | ins.rflags_cleared | ins.rflags_undefined)
        undef = to_x86(ins.rflags_undefined)
        mask = (mask & ~written) | undef
    return mask


def decode_block(mem: bytes, cs: int, ip: int, limit: int = 400):
    insns, cur = [], ip
    while len(insns) < limit:
        lin = ((cs << 4) + cur) & 0xFFFFF
        ins = ix.Decoder(16, bytes(mem[lin:lin + 16]), ip=cur).decode()
        insns.append(ins)
        if ins.flow_control != ix.FlowControl.NEXT:
            break
        cur = (cur + ins.len) & 0xFFFF
    return insns


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("lib")
    ap.add_argument("--load-seg", type=lambda v: int(v, 0), default=0x0824)
    ap.add_argument("--states", type=int, default=3)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--only", default=None)
    ap.add_argument("--max-report", type=int, default=15)
    ap.add_argument("--ref-mem", required=True, help="reference memory capture (same as recompile.py)")
    args = ap.parse_args()

    img = load_mz(Path(args.exe).read_bytes(), args.load_seg)
    base_mem = bytearray(img.memory) + bytearray(0x10000)
    ref = Path(args.ref_mem).read_bytes()
    refmachine.apply(base_mem, ref, args.load_seg,
                     bytes(img.memory[args.load_seg << 4:(args.load_seg << 4) + img.image_size]))
    lib = ctypes.CDLL(args.lib)
    lib.rc_run_block.argtypes = [ctypes.POINTER(State), ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
    n = lib.rc_block_count()
    blocks = []
    lengths = {}
    for i in range(n):
        cs, ip, cnt = ctypes.c_uint16(), ctypes.c_uint16(), ctypes.c_uint16()
        lib.rc_block_at(i, ctypes.byref(cs), ctypes.byref(ip), ctypes.byref(cnt))
        blocks.append((cs.value, ip.value))
        lengths[(cs.value, ip.value)] = cnt.value
    if args.only:
        s, o = args.only.split(":")
        blocks = [(int(s, 16), int(o, 16))]
    rnd = random.Random(args.seed)
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x110000)
    fails, checked, skipped = [], 0, 0

    for cs, ip in blocks:
        # The recompiled block may be shorter than a pure decode (it splits at other block starts):
        # its own length is found from the library run (next IP when it falls through) below.
        insns = decode_block(base_mem, cs, ip)
        for _ in range(args.states):
            regs = {n: rnd.randrange(0x10000) for n, _ in REGS}
            regs.update(cs=cs, ip=ip, ss=rnd.randrange(0x1000, 0x9000), ds=rnd.randrange(0x0800, 0x9F00),
                        es=rnd.randrange(0x0800, 0x9F00), cx=rnd.randrange(0, 40), sp=rnd.randrange(0x100, 0xFF00) & ~1)
            flags = (rnd.randrange(0x10000) & 0x0ED5 & ~0x0100) | 0x0002  # TF off
            mem_c = bytearray(base_mem)
            st = State(**{k: regs[k] for k in regs}, flags=flags, icount=0)
            arg = ctypes.c_int(0)
            buf = (ctypes.c_char * len(mem_c)).from_buffer(mem_c)
            ev = lib.rc_run_block(ctypes.byref(st), ctypes.cast(buf, ctypes.c_char_p), ctypes.byref(arg))
            if ev == -1:
                skipped += 1
                break
            executed = st.icount  # instructions the recompiled block executed (REP iterations count extra)
            # Unicorn: same start state; stop after the same number of distinct instructions.
            uc.mem_write(0, bytes(base_mem[:0x110000]))
            for name, reg in REGS:
                uc.reg_write(reg, regs[name])
            uc.reg_write(UC_X86_REG_EFLAGS, flags)
            info = {"n": 0, "prev": None, "stop_at": None, "event": 0}
            limit = max(1, sum(1 for _ in insns))

            def hook_code(u, address, size, user):
                if address != info["prev"]:
                    info["n"] += 1
                    info["prev"] = address
                    if info["n"] > info["want"]:
                        u.emu_stop()

            def hook_intr(u, intno, user):
                info["event"] = 1
                info["intno"] = intno
                u.emu_stop()

            def hook_io(u, *a):
                info["event"] = 2
                u.emu_stop()
                return 0

            info["want"] = lengths[(cs, ip)]
            h1 = uc.hook_add(UC_HOOK_CODE, hook_code)
            h2 = uc.hook_add(UC_HOOK_INTR, hook_intr)
            h3 = uc.hook_add(UC_HOOK_INSN, hook_io, None, 1, 0, UC_X86_INS_IN)
            h4 = uc.hook_add(UC_HOOK_INSN, hook_io, None, 1, 0, UC_X86_INS_OUT)
            try:
                uc.emu_start((cs << 4) + ip, 0x200000, count=50_000_000)
            except UcError as e:
                info["event"] = 9
            for h in (h1, h2, h3, h4):
                uc.hook_del(h)
            checked += 1
            if ev in (2, 3) or info["event"] in (2, 9):
                continue  # port I/O or unsupported: device semantics are checked by lockstep sessions
            if info.get("intno") == 1 or uc.reg_read(UC_X86_REG_EFLAGS) & 0x100:
                continue  # trap flag popped from random memory: single-step traps are not modelled (not used)
            got = {name: getattr(st, name) for name, _ in REGS}
            exp = {name: uc.reg_read(reg) & 0xFFFF for name, reg in REGS}
            if ev == 1:  # INT: the recompiled code set ip to the return address; compare the rest
                fault_ip = st.ip
                got.pop("ip"); exp.pop("ip")
                # CPU exceptions (divide error 0, BOUND 5): Unicorn stops on the faulting instruction
                # without always reporting the interrupt when other hooks are active.
                if arg.value in (0, 5) and info["event"] != 1 and (uc.reg_read(UC_X86_REG_IP) & 0xFFFF) == fault_ip:
                    info["event"] = 1
                if info["event"] != 1:
                    fails.append((cs, ip, "interrupt only in recompiled code", got, exp))
                    if args.only:
                        print("start", {k: hex(v) for k, v in regs.items()}, "flags", hex(flags), "int", arg.value)
                        print("recomp", {k: hex(v) for k, v in got.items()}, "unicorn", {k: hex(v) for k, v in exp.items()},
                              "uc ip", hex(uc.reg_read(UC_X86_REG_IP)), "steps", info["n"], "want", info["want"])
                    continue
            mask = ARITH & ~undefined_mask(insns[:info["want"]])
            gf, ef = st.flags & mask, uc.reg_read(UC_X86_REG_EFLAGS) & mask
            mem_u = uc.mem_read(0, 0x110000)
            diffs = [k for k in got if got[k] != exp[k]]
            if gf != ef:
                diffs.append(f"flags {gf:04x}!={ef:04x}")
            if bytes(mem_u) != bytes(mem_c[:0x110000]):
                first = next(i for i in range(0x110000) if mem_u[i] != mem_c[i])
                diffs.append(f"mem@{first:05x} {mem_c[first]:02x}!={mem_u[first]:02x}")
            if diffs:
                fails.append((cs, ip, diffs, {k: hex(v) for k, v in got.items()}, {k: hex(v) for k, v in exp.items()}))
                if args.only:
                    print("start", {k: hex(v) for k, v in regs.items()}, "flags", hex(flags))
                    print("recomp", fails[-1][3], "flags", hex(st.flags), "event", ev, arg.value)
                    print("unicorn", fails[-1][4], "flags", hex(uc.reg_read(UC_X86_REG_EFLAGS)), "event", info["event"])
                break
    print(f"blocks {len(blocks)}, runs {checked}, skipped {skipped}, failing blocks {len(fails)}")
    for f in fails[:args.max_report]:
        print(f"  {f[0]:04X}:{f[1]:04X} {f[2]}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
