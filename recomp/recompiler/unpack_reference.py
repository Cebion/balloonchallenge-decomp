#!/usr/bin/env python3
"""Rebuild the reference machine at the unpacked entry from a distributable reference and the packed EXE.

    python3 unpack_reference.py PACKED.EXE BLANK_REF.bin OUT.bin --load-seg 0x0824 --entry 21E5:0944

A reference capture (DOSBox-X memory at the program's unpacked entry, refmachine.py) contains the program
and whatever its unpackers (PKLITE, EXEPACK, ...) left in memory, which the program may read. A copy with
[load_seg*16, A0000) blanked holds no program bytes and can be distributed. This tool reloads the packed EXE
into the blanked copy as DOS does (module at load_seg, relocations, initial registers) and runs its own
unpacker code under Unicorn until the entry: the result is the capture again (Balloon Challenge: identical,
0 bytes differ). Stops with an error on any interrupt the unpacker issues, so a different unpacker is
noticed instead of silently producing other memory.
"""
import argparse
import struct
import sys
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_HOOK_INTR, UC_MODE_16, Uc
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CS, UC_X86_REG_CX, UC_X86_REG_DS,
                               UC_X86_REG_DX, UC_X86_REG_ES, UC_X86_REG_IP, UC_X86_REG_SP, UC_X86_REG_SS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("blank")
    ap.add_argument("out")
    ap.add_argument("--load-seg", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--entry", required=True, help="unpacked entry SSSS:OOOO")
    a = ap.parse_args()
    exe = Path(a.exe).read_bytes()
    mem = bytearray(Path(a.blank).read_bytes())
    if exe[:2] not in (b"MZ", b"ZM"):
        raise SystemExit("not an MZ executable")
    last, pages, nrel, hdr, _minal, _maxal, ss, sp, _csum, ip, cs, reloff = struct.unpack_from("<12H", exe, 2)
    size = (pages - 1) * 512 + last if last else pages * 512
    module = exe[hdr * 16:size]
    load, psp = a.load_seg, a.load_seg - 0x10
    if any(mem[load * 16:0xA0000]):
        raise SystemExit("the reference is not blanked above the load segment")
    mem[load * 16:load * 16 + len(module)] = module
    for i in range(nrel):
        off, seg = struct.unpack_from("<HH", exe, reloff + 4 * i)
        p = (load + seg) * 16 + off
        struct.pack_into("<H", mem, p, (struct.unpack_from("<H", mem, p)[0] + load) & 0xFFFF)
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, len(mem) + (-len(mem) % 0x1000))
    uc.mem_write(0, bytes(mem))
    for r, v in ((UC_X86_REG_CS, (load + cs) & 0xFFFF), (UC_X86_REG_IP, ip), (UC_X86_REG_SS, (load + ss) & 0xFFFF),
                 (UC_X86_REG_SP, sp), (UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_AX, 0),
                 (UC_X86_REG_BX, 0), (UC_X86_REG_CX, 0), (UC_X86_REG_DX, psp)):
        uc.reg_write(r, v)
    seen = []
    uc.hook_add(UC_HOOK_INTR, lambda u, n, d: (seen.append(n), u.emu_stop()))
    es, eo = (int(v, 16) for v in a.entry.split(":"))
    uc.emu_start(((load + cs) & 0xFFFF) * 16 + ip, es * 16 + eo, count=200_000_000)
    at = (uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
    if seen or at != (es, eo):
        raise SystemExit("unpacker did not reach %s (stopped at %04X:%04X, interrupts %s)" % (a.entry, at[0], at[1], seen))
    mem[:0xA0000] = uc.mem_read(0, 0xA0000)
    Path(a.out).write_bytes(bytes(mem))
    print("reference at %s: %s" % (a.entry, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
