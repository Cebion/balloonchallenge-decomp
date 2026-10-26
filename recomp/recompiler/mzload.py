"""Load an unpacked DOS MZ image into a 1 MB real-mode memory array, the way DOS does.

Used by the recompiler (discovery, translation) and by tests. The load segment is a parameter so the
recompiled program can use the same addresses as a DOSBox-X reference run.
"""
import struct
from dataclasses import dataclass, field


@dataclass
class MZImage:
    load_seg: int                 # paragraph where the load module starts (PSP + 0x10)
    memory: bytearray             # 1 MB, image copied in, relocations applied
    image_size: int               # bytes of the load module
    entry: tuple                  # (cs, ip) after relocation
    stack: tuple                  # (ss, sp) after relocation
    relocs: list = field(default_factory=list)   # linear addresses of relocated segment words
    min_alloc: int = 0
    max_alloc: int = 0

    @property
    def psp_seg(self) -> int:
        return self.load_seg - 0x10

    def linear(self, seg: int, off: int) -> int:
        return ((seg << 4) + off) & 0xFFFFF


def load_mz(data: bytes, load_seg: int) -> MZImage:
    if data[:2] not in (b"MZ", b"ZM"):
        raise ValueError("not an MZ executable")
    last, pages, nrel, hdrpara, minalloc, maxalloc, ss, sp, _csum, ip, cs, reloff = struct.unpack_from("<12H", data, 2)
    size = (pages - 1) * 512 + last if last else pages * 512
    hdr = hdrpara * 16
    module = data[hdr:size]
    mem = bytearray(1 << 20)
    base = load_seg << 4
    mem[base:base + len(module)] = module
    relocs = []
    for i in range(nrel):
        off, seg = struct.unpack_from("<HH", data, reloff + 4 * i)
        lin = base + (seg << 4) + off
        val = struct.unpack_from("<H", mem, lin)[0]
        struct.pack_into("<H", mem, lin, (val + load_seg) & 0xFFFF)
        relocs.append(lin)
    return MZImage(load_seg=load_seg, memory=mem, image_size=len(module),
                   entry=((cs + load_seg) & 0xFFFF, ip), stack=((ss + load_seg) & 0xFFFF, sp),
                   relocs=relocs, min_alloc=minalloc, max_alloc=maxalloc)
