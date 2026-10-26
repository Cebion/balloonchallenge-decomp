"""Static code discovery for a loaded MZ image (recursive descent over basic blocks).

Blocks are keyed by (cs, ip). A block ends at a control transfer. Successors:
  - direct jmp/jcc/call/loop targets (near: same cs; far: absolute after relocation),
  - the fall-through after conditional branches and after calls (the callee is assumed to return),
  - INT n continues after the instruction (the runtime decides at run time),
  - a DOSBox-X callback `FE 38 nn nn` (in the reference BIOS stubs) ends the block; the native callback
    may adjust IP (wait loops, DOS overhead), so its continuation is resolved at run time.
Indirect transfers (jmp/call through register or memory, ret, iret, retf) end a block without static
successors; the dispatcher resolves them at run time, and discovery runs add missing targets to entries.
"""
import iced_x86 as ix

from mzload import MZImage

FC = ix.FlowControl


class Block:
    __slots__ = ("cs", "ip", "insns", "succ", "kind", "callback")

    def __init__(self, cs, ip):
        self.cs, self.ip, self.insns, self.succ, self.kind = cs, ip, [], [], "fall"
        self.callback = None  # (number, ip after the 4-byte callback opcode) for kind "callback"


def decode_at(img: MZImage, cs: int, ip: int) -> ix.Instruction:
    lin = img.linear(cs, ip)
    dec = ix.Decoder(16, bytes(img.memory[lin:lin + 16]), ip=ip)
    return dec.decode()


def fpu_continuation(img: MZImage, cs: int, ip: int, ins: ix.Instruction):
    """Return the IP after an emulated x87 instruction for INT 34h-3Bh/3Dh, else None.

    INT 34h..3Bh replaces WAIT + ESC (D8..DF): the bytes after `CD 3n` are the ESC operand bytes, so
    the emulated instruction is D8+(n-34h) followed by them; the total length is unchanged (2 bytes for
    `CD 3n` vs. 1 for WAIT + 1 for ESC). INT 3Dh replaces WAIT NOP (2 bytes). INT 3Ch (segment
    override form) is resolved dynamically.
    """
    if ins.mnemonic != ix.Mnemonic.INT:
        return None
    n = ins.immediate8
    if n == 0x3D:
        return (ip + 2) & 0xFFFF
    if 0x34 <= n <= 0x3B:
        lin = img.linear(cs, ip)
        esc = bytes([0xD8 + (n - 0x34)]) + bytes(img.memory[lin + 2:lin + 16])
        x87 = ix.Decoder(16, esc, ip=0).decode()
        return (ip + 1 + x87.len) & 0xFFFF
    return None


def far_target(ins: ix.Instruction) -> tuple:
    return ins.far_branch_selector, ins.far_branch16


def discover(img: MZImage, seeds, limit: int = 200000):
    """Return dict (cs, ip) -> Block for everything reachable from seeds."""
    blocks = {}
    work = list(seeds)
    starts = set(seeds)
    while work and len(blocks) < limit:
        cs, ip = work.pop()
        if (cs, ip) in blocks:
            continue
        b = Block(cs, ip)
        cur = ip
        while True:
            lin = img.linear(cs, cur)
            if img.memory[lin] == 0xFE and img.memory[lin + 1] == 0x38:
                b.callback = (img.memory[lin + 2] | (img.memory[lin + 3] << 8), (cur + 4) & 0xFFFF)
                b.succ.append((cs, b.callback[1]))
                b.kind = "callback"
                break
            ins = decode_at(img, cs, cur)
            if ins.code == ix.Code.INVALID:
                b.kind = "invalid"
                break
            b.insns.append(ins)
            nxt = (cur + ins.len) & 0xFFFF
            fc = ins.flow_control
            if fc == FC.INTERRUPT and ins.mnemonic not in (ix.Mnemonic.IRET, ix.Mnemonic.IRETD):
                # INT ends the block; the handler returns to the next instruction, except the Microsoft
                # 8087-emulator interrupts, which skip the operand bytes of the emulated x87 instruction.
                b.succ.append((cs, fpu_continuation(img, cs, cur, ins) or nxt))
                b.kind = "int"
                break
            if fc == FC.NEXT:
                cur = nxt
                if (cs, cur) in starts:  # another block starts here: split
                    b.succ.append((cs, cur))
                    break
                continue
            if fc == FC.UNCONDITIONAL_BRANCH:
                if ins.op0_kind == ix.OpKind.FAR_BRANCH16:
                    b.succ.append(far_target(ins))
                else:
                    b.succ.append((cs, ins.near_branch16))
                b.kind = "jmp"
            elif fc == FC.CONDITIONAL_BRANCH:
                b.succ += [(cs, ins.near_branch16), (cs, nxt)]
                b.kind = "jcc"
            elif fc == FC.CALL:
                if ins.op0_kind == ix.OpKind.FAR_BRANCH16:
                    b.succ.append(far_target(ins))
                else:
                    b.succ.append((cs, ins.near_branch16))
                b.succ.append((cs, nxt))
                b.kind = "call"
            elif fc == FC.INDIRECT_CALL:
                b.succ.append((cs, nxt))
                b.kind = "icall"
            elif fc == FC.INDIRECT_BRANCH:
                b.kind = "ijmp"
            elif fc == FC.RETURN:
                b.kind = "ret"
            elif fc == FC.INTERRUPT:  # IRET
                b.kind = "iret"
            else:
                b.kind = "other"
            break
        blocks[(cs, ip)] = b
        for s in b.succ:
            if s not in blocks:
                starts.add(s)
                work.append(s)
    return blocks
