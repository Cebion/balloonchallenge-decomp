"""The reference machine (DOSBox-X) as captured at the program's unpacked entry: one owner for which memory
regions the port takes from it, where its interrupt vectors point, and which callback serves which vector.

The port runs the reference's own BIOS/DOS entry stubs (recompiled like the program). DOSBox-X stubs are
x86 code around a 4-byte callback opcode `FE 38 nn nn`; the runtime implements callback nn natively with
DOSBox-X's semantics, including its IP adjustments (wait loops, DOS overhead). Vectors, ROM bytes (machine
model, BIOS date) and stub instruction counts are therefore those of the reference.
"""
import struct
from pathlib import Path

ROM_REGIONS = ((0xC0000, 0xC8000), (0xF0000, 0x110000))  # video BIOS, system BIOS, HMA (A20 on)
CALLBACK_OPCODE = b"\xFE\x38"


def regions(psp_seg: int):
    """Linear ranges copied from the capture: all conventional memory (IVT, BDA, DOS data, MCBs,
    environment, PSP, and above the program image whatever the unpackers left there: uninitialised
    memory the program may read) and the ROM/HMA areas. The program image itself is identical in the
    capture and in the executable (mzload verification); the runtime places the executable's image."""
    del psp_seg  # the program's position does not change what is copied
    return ((0, 0xA0000),) + ROM_REGIONS


def apply(memory: bytearray, ref: bytes, load_seg: int, image: bytes) -> None:
    """The machine the program starts in: the reference regions copied from the capture, then the program
    image from the executable on top (the capture's copy of the image may be blanked: a distributable
    reference machine holds no program bytes)."""
    for lo, hi in regions(load_seg - 0x10):
        memory[lo:hi] = ref[lo:hi]
    memory[load_seg << 4:(load_seg << 4) + len(image)] = image


def vectors(mem: bytes):
    return [struct.unpack_from("<HH", mem, n * 4)[::-1] for n in range(256)]  # (seg, off)


def callback_at(mem: bytes, seg: int, off: int, limit: int = 16):
    """Callback number of the stub at seg:off (first FE 38 within `limit` bytes; one far JMP followed)."""
    a = (seg << 4) + off
    if mem[a] == 0xEA:
        off, seg = struct.unpack_from("<HH", mem, a + 1)
        a = (seg << 4) + off
    i = mem.find(CALLBACK_OPCODE, a, a + limit + 2)
    return struct.unpack_from("<H", mem, i + 2)[0] if i >= 0 else None


def callback_map(mem: bytes):
    """{callback number: vector} from the stubs the reference IVT points at. Several vectors may share one
    default stub; the lowest vector number names it (its service is the same)."""
    out = {}
    for n, (seg, off) in enumerate(vectors(mem)):
        if seg == 0 and off == 0:
            continue
        cb = callback_at(mem, seg, off)
        if cb is not None and cb not in out:
            out[cb] = n
    return out


def realint_trampoline(mem: bytes):
    """The reference's trampoline for interrupts its own native code issues (DOSBox-X CALLBACK_RunRealInt):
    256 entries of 6 bytes `INT n; FE 38 cb` in the system ROM, cb the stop callback. Returns (seg, off,
    stop callback); raises if the table is not there."""
    i = mem.find(bytes([0xCD, 0x00]) + CALLBACK_OPCODE, 0xF0000, 0x100000)
    if i < 0:
        raise ValueError("no interrupt trampoline in the reference ROM")
    stop = struct.unpack_from("<H", mem, i + 4)[0]
    for n in range(256):
        e = i + n * 6
        if mem[e:e + 2] != bytes([0xCD, n]) or mem[e + 2:e + 4] != CALLBACK_OPCODE or struct.unpack_from("<H", mem, e + 4)[0] != stop:
            raise ValueError(f"interrupt trampoline entry {n:02X} at {e:05X} is not INT {n:02X}; callback {stop}")
    return 0xF000, i - 0xF0000, stop


def idle_code(mem: bytes):
    """The reference's idle routine (DOSBox-X CALLBACK_Idle: 12 NOPs, then a stop callback), run by its native
    code while it waits, e.g. for emulated disk transfer time. Returns (seg, off, callback)."""
    i = mem.find(bytes([0x90] * 12) + CALLBACK_OPCODE, 0xF0000, 0x100000)
    if i < 0:
        raise ValueError("no idle routine in the reference ROM")
    return 0xF000, i - 0xF0000, struct.unpack_from("<H", mem, i + 14)[0]


def stub_entries(mem: bytes, psp_seg: int):
    """Vector targets outside the program: entry points of the reference's handler stubs, and the interrupt
    trampoline's entries (run when the reference's native code issues an interrupt)."""
    seg, off, _ = realint_trampoline(mem)
    out = {(seg, off + n * 6) for n in range(256)}
    out.add(idle_code(mem)[:2])
    for seg, off in vectors(mem):
        a = (seg << 4) + off
        if (seg, off) != (0, 0) and any(lo <= a < hi for lo, hi in regions(psp_seg)):
            out.add((seg, off))
    return out


def entry_ms(break_log: str, entry: str) -> float:
    """Emulated time (ms since power-on) when the reference stopped at the program entry: the first line
    for `entry` (SSSS:OOOO) in a DOSBox-X REA_LOG_BREAK log ("SSSS:OOOO ms pit0 START DELAY" per debugger
    entry)."""
    return _break_at(break_log, entry)[0]


def pit0_phase(break_log: str, entry: str) -> float:
    """Time (ms) since the current PIT channel 0 period began, at the program entry: the phase of the BIOS
    timer tick (IRQ0 fires at the end of each period). From the channel's time base and period in the
    REA_LOG_BREAK line."""
    ms, start, delay = _break_at(break_log, entry)
    return (ms - start) % delay


def _break_at(break_log: str, entry: str):
    for line in Path(break_log).read_text().splitlines():
        f = line.split()
        if f[0] == entry:
            if len(f) < 5:
                raise ValueError(f"{break_log}: no PIT channel 0 state (older instrumentation)")
            return float(f[1]), float(f[3]), float(f[4])
    raise ValueError(f"{entry} not in {break_log}")


def vga_timing(vga_log: str, entry: float):
    """Display timing from a DOSBox-X REA_LOG_VGA log, relative to the program entry: one tuple
    (from_ms, t0_ms, vtotal, htotal, hblkstart, hblkend, vrstart, vrend, vdend) per display setup ("D"
    lines). Frames start at t0 + n * vtotal (t0: vga_mode_time_base, the frame grid origin; the logged
    frame starts ("F") stay on that grid within nanoseconds). The setup in effect at the entry applies
    from the start (from_ms -inf)."""
    out = []
    for line in Path(vga_log).read_text().splitlines():
        f = line.split()
        if not f or f[0] != "D":
            continue
        v = dict(zip(f[2::2], f[3::2]))  # "vtotal X htotal X hblk A B ..." -> pairs, hblk/vr take two values
        hblk, vr = f[f.index("hblk") + 1:f.index("hblk") + 3], f[f.index("vr") + 1:f.index("vr") + 3]
        row = (float(f[1]) - entry, float(v["base"]) - entry, float(v["vtotal"]), float(v["htotal"]),
               float(hblk[0]), float(hblk[1]), float(vr[0]), float(vr[1]), float(v["vdend"]))
        if row[0] <= 0:
            out = [row]
        else:
            out.append(row)
    if not out:
        raise ValueError(f"no display setup in {vga_log}")
    out[0] = (-1e18,) + out[0][1:]
    return out


def write_vga_timing(rows, path: str) -> None:
    """The runtime's --vga-timing file: one line of 9 numbers per row of vga_timing()."""
    Path(path).write_text("".join(" ".join(f"{x:.12f}" if abs(x) < 1e17 else "-1e18" for x in r) + "\n" for r in rows))
