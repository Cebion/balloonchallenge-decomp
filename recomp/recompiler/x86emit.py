"""Translate decoded 16-bit x86 instructions (iced-x86) into C++ statements over runtime/cpu.h.

Each basic block becomes one C++ function. Control transfers set c.cs / c.ip and return to the
dispatcher; the instruction count is added per executed instruction (c.icount), the clock used for
timers, matching DOSBox-X `cycles=fixed`.
"""
import iced_x86 as ix

R = ix.Register
K = ix.OpKind
M = ix.Mnemonic

REG = {}
for name in ("AL CL DL BL AH CH DH BH AX CX DX BX SP BP SI DI ES CS SS DS").split():
    REG[getattr(R, name)] = "c." + name.lower()
SEG = {R.ES: "c.es", R.CS: "c.cs", R.SS: "c.ss", R.DS: "c.ds"}
SIZE8 = {R.AL, R.CL, R.DL, R.BL, R.AH, R.CH, R.DH, R.BH}

MN = {v: k for k, v in M.__dict__.items() if isinstance(v, int)}


class Unsupported(Exception):
    pass


def mem_size(ins) -> int:
    return ix.MemorySizeInfo(ins.memory_size).size


def ea(ins) -> str:
    parts = []
    if ins.memory_base != R.NONE:
        parts.append(REG[ins.memory_base])
    if ins.memory_index != R.NONE:
        parts.append(REG[ins.memory_index])
    disp = ins.memory_displacement & 0xFFFF
    if disp or not parts:
        parts.append(f"0x{disp:X}")
    return "uint16_t(" + " + ".join(parts) + ")"


def seg_of(ins) -> str:
    return SEG[ins.memory_segment]


def op_size(ins, i) -> int:
    k = ins.op_kind(i)
    if k == K.REGISTER:
        return 1 if ins.op_register(i) in SIZE8 else 2
    if k == K.MEMORY:
        return mem_size(ins)
    if k in (K.IMMEDIATE8,):
        return 1
    return 2


def read(ins, i, size=None) -> str:
    k = ins.op_kind(i)
    if k == K.REGISTER:
        return REG[ins.op_register(i)]
    if k == K.MEMORY:
        size = size or mem_size(ins)
        return f"rd{8 * size}(c, {seg_of(ins)}, {ea(ins)})"
    if k in (K.IMMEDIATE8, K.IMMEDIATE16, K.IMMEDIATE8TO16, K.IMMEDIATE8_2ND):
        v = ins.immediate(i) if k != K.IMMEDIATE8_2ND else ins.immediate8_2nd
        return f"0x{v & 0xFFFF:X}"
    raise Unsupported(f"read op kind {k}")


def write(ins, i, value: str, size=None) -> str:
    k = ins.op_kind(i)
    if k == K.REGISTER:
        return f"{REG[ins.op_register(i)]} = {value};"
    if k == K.MEMORY:
        size = size or mem_size(ins)
        return f"wr{8 * size}(c, {seg_of(ins)}, {ea(ins)}, {value});"
    raise Unsupported(f"write op kind {k}")


def rmw(ins, value_of) -> str:
    """Read-modify-write on operand 0; value_of(src_expr) gives the new value expression.
    Memory operands are read once into a local so the address is computed once."""
    sz = op_size(ins, 0)
    t = "uint8_t" if sz == 1 else "uint16_t"
    if ins.op_kind(0) == K.MEMORY:
        return (f"{{ uint16_t a_ = {ea(ins)}; {t} v_ = rd{8*sz}(c, {seg_of(ins)}, a_); "
                f"wr{8*sz}(c, {seg_of(ins)}, a_, {value_of('v_')}); }}")
    r = REG[ins.op_register(0)]
    return f"{r} = {value_of(r)};"


ALU = {M.ADD: "add", M.ADC: "adc", M.SUB: "sub", M.SBB: "sbb", M.AND: "and", M.OR: "or", M.XOR: "xor"}
SHIFT = {M.SHL: "SHL", M.SAL: "SHL", M.SHR: "SHR", M.SAR: "SAR", M.ROL: "ROL", M.ROR: "ROR",
         M.RCL: "RCL", M.RCR: "RCR"}
JCC = {
    M.JO: "(c.flags & F_OF)", M.JNO: "!(c.flags & F_OF)", M.JB: "(c.flags & F_CF)", M.JAE: "!(c.flags & F_CF)",
    M.JE: "(c.flags & F_ZF)", M.JNE: "!(c.flags & F_ZF)", M.JBE: "(c.flags & (F_CF | F_ZF))",
    M.JA: "!(c.flags & (F_CF | F_ZF))", M.JS: "(c.flags & F_SF)", M.JNS: "!(c.flags & F_SF)",
    M.JP: "(c.flags & F_PF)", M.JNP: "!(c.flags & F_PF)",
    M.JL: "(!!(c.flags & F_SF) != !!(c.flags & F_OF))", M.JGE: "(!!(c.flags & F_SF) == !!(c.flags & F_OF))",
    M.JLE: "((c.flags & F_ZF) || !!(c.flags & F_SF) != !!(c.flags & F_OF))",
    M.JG: "(!(c.flags & F_ZF) && !!(c.flags & F_SF) == !!(c.flags & F_OF))",
}


def goto(cs: str, ip: int) -> str:
    return f"c.cs = {cs}; c.ip = 0x{ip:04X}; return;"


def emit(ins, cs_val: int) -> list:
    """Return C++ lines for one instruction. Block-ending instructions set CS:IP and return.
    Raises Unsupported for anything outside the 16-bit model (e.g. 32-bit registers in 386 BIOS code)."""
    try:
        return _emit(ins, cs_val)
    except KeyError as e:
        raise Unsupported(f"{ix_name(ins)} operand {e}") from None


def ix_name(ins) -> str:
    return MN.get(ins.mnemonic, str(ins.mnemonic)) if isinstance(MN, dict) else str(ins.mnemonic)


def _emit(ins, cs_val: int) -> list:
    mn = ins.mnemonic
    nxt = (ins.ip + ins.len) & 0xFFFF
    here = f"0x{ins.ip:04X}"
    out = [f"/* {ins.ip:04X} */ c.icount++;"]
    add = out.append
    rep = ins.has_rep_prefix or ins.has_repe_prefix
    repne = ins.has_repne_prefix

    if mn == M.MOV:
        if ins.op_kind(0) == K.REGISTER and ins.op_register(0) in SEG.keys():
            add(f"{REG[ins.op_register(0)]} = {read(ins, 1, 2)};")
        else:
            add(write(ins, 0, read(ins, 1)))
    elif mn in ALU:
        sz = op_size(ins, 0); f = f"{ALU[mn]}{8*sz}"
        add(rmw(ins, lambda d: f"{f}(c, {d}, {read(ins, 1, sz)})"))
    elif mn == M.CMP:
        sz = op_size(ins, 0); add(f"(void)sub{8*sz}(c, {read(ins, 0)}, {read(ins, 1, sz)});")
    elif mn == M.TEST:
        sz = op_size(ins, 0); add(f"(void)and{8*sz}(c, {read(ins, 0)}, {read(ins, 1, sz)});")
    elif mn in (M.INC, M.DEC, M.NEG, M.NOT):
        sz = op_size(ins, 0)
        if mn == M.NOT:
            t = "uint8_t" if sz == 1 else "uint16_t"
            add(rmw(ins, lambda d: f"{t}(~{d})"))
        else:
            add(rmw(ins, lambda d: f"{MN[mn].lower()}{8*sz}(c, {d})"))
    elif mn in SHIFT:
        sz = op_size(ins, 0)
        cnt = read(ins, 1) if ins.op_count > 1 else "1"
        add(rmw(ins, lambda d: f"{SHIFT[mn]}{8*sz}(c, {d}, uint8_t({cnt}))"))
    elif mn == M.XCHG:
        sz = op_size(ins, 0); t = "uint8_t" if sz == 1 else "uint16_t"
        add(f"{{ {t} t0_ = {read(ins, 0)}; {t} t1_ = {read(ins, 1)}; {write(ins, 0, 't1_')} {write(ins, 1, 't0_')} }}")
    elif mn == M.LEA:
        add(f"{REG[ins.op_register(0)]} = {ea(ins)};")
    elif mn in (M.LES, M.LDS):
        segr = "c.es" if mn == M.LES else "c.ds"
        add(f"{{ uint16_t a_ = {ea(ins)}; uint16_t s_ = {seg_of(ins)}; {REG[ins.op_register(0)]} = rd16(c, s_, a_); "
            f"{segr} = rd16(c, s_, uint16_t(a_ + 2)); }}")
    elif mn == M.PUSH:
        k = ins.op_kind(0)
        if k == K.REGISTER and ins.op_register(0) == R.SP:
            add("push16(c, c.sp);")   # 286+: pushes the value before the decrement
        else:
            add(f"push16(c, {read(ins, 0, 2)});")
    elif mn == M.POP:
        if ins.op_kind(0) == K.REGISTER:
            add(f"{REG[ins.op_register(0)]} = pop16(c);")
        else:
            add(f"{{ uint16_t v_ = pop16(c); {write(ins, 0, 'v_', 2)} }}")
    elif mn == M.PUSHF:
        add("push16(c, get_flags(c));")
    elif mn == M.POPF:
        add("set_flags(c, pop16(c));")
    elif mn == M.SAHF:
        add("c.flags = uint16_t((c.flags & 0xFF00) | (c.ah & 0xD5) | 0x02);")
    elif mn == M.LAHF:
        add("c.ah = uint8_t(get_flags(c));")
    elif mn in (M.CLC, M.STC, M.CMC, M.CLD, M.STD, M.CLI, M.STI):
        add({M.CLC: "setf(c, F_CF, false);", M.STC: "setf(c, F_CF, true);", M.CMC: "c.flags ^= F_CF;",
             M.CLD: "setf(c, F_DF, false);", M.STD: "setf(c, F_DF, true);", M.CLI: "setf(c, F_IF, false);",
             M.STI: "setf(c, F_IF, true);"}[mn])
    elif mn == M.CBW:
        add("c.ax = uint16_t(int16_t(int8_t(c.al)));")
    elif mn == M.CWD:
        add("c.dx = (c.ax & 0x8000) ? 0xFFFF : 0;")
    elif mn in (M.MUL, M.IMUL) and ins.op_count == 1:
        sz = op_size(ins, 0); add(f"{MN[mn].lower()}{8*sz}(c, {read(ins, 0)});")
    elif mn == M.IMUL:
        src = read(ins, 1) if ins.op_count == 2 else read(ins, 1)
        imm = read(ins, 2) if ins.op_count == 3 else read(ins, 0)
        add(f"{REG[ins.op_register(0)]} = imul16_3(c, {src}, {imm});")
    elif mn in (M.DIV, M.IDIV):
        sz = op_size(ins, 0)
        add(f"if (!{MN[mn].lower()}{8*sz}(c, {read(ins, 0)})) {{ c.ip = {here}; rt_raise(c, 0); return; }}")
    elif mn == M.AAM:
        add(f"if (!aam(c, 0x{ins.immediate(0) & 0xFF:X})) {{ c.ip = {here}; rt_raise(c, 0); return; }}")
    elif mn == M.AAD:
        add(f"aad(c, 0x{ins.immediate(0) & 0xFF:X});")
    elif mn in (M.DAA, M.DAS, M.AAA):
        add(f"{MN[mn].lower()}(c);")
    elif mn == M.XLATB:
        add(f"c.al = rd8(c, {seg_of(ins)}, uint16_t(c.bx + c.al));")
    elif mn == M.NOP:
        pass
    elif mn == M.LEAVE:
        add("c.sp = c.bp; c.bp = pop16(c);")
    elif mn == M.BOUND:
        add(f"{{ uint16_t a_ = {ea(ins)}; int16_t lo_ = int16_t(rd16(c, {seg_of(ins)}, a_)); "
            f"int16_t hi_ = int16_t(rd16(c, {seg_of(ins)}, uint16_t(a_ + 2))); int16_t v_ = int16_t({REG[ins.op_register(0)]}); "
            f"if (v_ < lo_ || v_ > hi_) {{ c.ip = {here}; rt_raise(c, 5); return; }} }}")
    elif mn in (M.IN, M.OUT):
        port = read(ins, 1 if mn == M.IN else 0)
        if mn == M.IN:
            dst = REG[ins.op_register(0)]
            add(f"{dst} = {'rt_in8' if dst == 'c.al' else 'rt_in16'}(c, uint16_t({port}));")
        else:
            src = REG[ins.op_register(1)]
            add(f"{'rt_out8' if src == 'c.al' else 'rt_out16'}(c, uint16_t({port}), {src});")
    elif mn in (M.MOVSB, M.MOVSW, M.STOSB, M.STOSW, M.LODSB, M.LODSW, M.SCASB, M.SCASW, M.CMPSB, M.CMPSW,
                M.OUTSB, M.INSB):
        name = MN[mn].lower()
        src_seg = SEG[ins.memory_segment] if mn in (M.MOVSB, M.MOVSW, M.LODSB, M.LODSW, M.CMPSB, M.CMPSW, M.OUTSB) else "c.ds"
        mode = "REP_NE" if repne else ("REP" if rep else "ONCE")
        add(f"str_{name}(c, {src_seg}, {mode});")
    elif mn in (M.FXCH, M.FCOM, M.FNINIT, M.FNSTSW, M.FNSTCW, M.FLDCW, M.FINIT, M.FSTSW, M.FSTCW) or \
            MN[mn].startswith("F"):
        add(f"/* x87 {MN[mn]}: no coprocessor present, ESC opcodes do nothing */")
    elif mn == M.WAIT:
        pass
    # ---- control transfers (end the block) ----
    elif mn == M.JMP:
        k = ins.op0_kind
        if k == K.NEAR_BRANCH16:
            add(goto("c.cs", ins.near_branch16))
        elif k == K.FAR_BRANCH16:
            add(f"c.cs = 0x{ins.far_branch_selector:04X}; c.ip = 0x{ins.far_branch16:04X}; return;")
        elif k == K.REGISTER:
            add(f"c.ip = {REG[ins.op_register(0)]}; return;")
        elif k == K.MEMORY and mem_size(ins) == 4:
            add(f"{{ uint16_t a_ = {ea(ins)}; uint16_t s_ = {seg_of(ins)}; c.ip = rd16(c, s_, a_); c.cs = rd16(c, s_, uint16_t(a_ + 2)); return; }}")
        else:
            add(f"c.ip = {read(ins, 0, 2)}; return;")
    elif mn in JCC:
        add(f"if ({JCC[mn]}) {{ {goto('c.cs', ins.near_branch16)} }}")
        add(goto("c.cs", nxt))
    elif mn in (M.LOOP, M.LOOPE, M.LOOPNE, M.JCXZ):
        cond = {M.LOOP: "--c.cx != 0", M.LOOPE: "--c.cx != 0 && (c.flags & F_ZF)",
                M.LOOPNE: "--c.cx != 0 && !(c.flags & F_ZF)", M.JCXZ: "c.cx == 0"}[mn]
        add(f"if ({cond}) {{ {goto('c.cs', ins.near_branch16)} }}")
        add(goto("c.cs", nxt))
    elif mn == M.CALL:
        k = ins.op0_kind
        if k == K.NEAR_BRANCH16:
            add(f"push16(c, 0x{nxt:04X}); {goto('c.cs', ins.near_branch16)}")
        elif k == K.FAR_BRANCH16:
            add(f"push16(c, c.cs); push16(c, 0x{nxt:04X}); c.cs = 0x{ins.far_branch_selector:04X}; "
                f"c.ip = 0x{ins.far_branch16:04X}; return;")
        elif k == K.MEMORY and mem_size(ins) == 4:
            add(f"{{ uint16_t a_ = {ea(ins)}; uint16_t s_ = {seg_of(ins)}; uint16_t o_ = rd16(c, s_, a_); "
                f"uint16_t g_ = rd16(c, s_, uint16_t(a_ + 2)); push16(c, c.cs); push16(c, 0x{nxt:04X}); "
                f"c.cs = g_; c.ip = o_; return; }}")
        else:
            add(f"{{ uint16_t t_ = {read(ins, 0, 2)}; push16(c, 0x{nxt:04X}); c.ip = t_; return; }}")
    elif mn == M.RET:
        n = ins.immediate16 if ins.op_count else 0
        add(f"c.ip = pop16(c); c.sp = uint16_t(c.sp + {n}); return;")
    elif mn == M.RETF:
        n = ins.immediate16 if ins.op_count else 0
        add(f"c.ip = pop16(c); c.cs = pop16(c); c.sp = uint16_t(c.sp + {n}); return;")
    elif mn == M.IRET:
        add("c.ip = pop16(c); c.cs = pop16(c); set_flags(c, pop16(c)); return;")
    elif mn in (M.INT, M.INT3, M.INTO):
        n = ins.immediate8 if mn == M.INT else (3 if mn == M.INT3 else 4)
        if mn == M.INTO:
            add(f"if (!(c.flags & F_OF)) {{ {goto('c.cs', nxt)} }}")
        if n == 0x3D:
            # Microsoft 8087-emulator "FWAIT": its handler rewrites the INT 3Dh into 89 C0 (mov ax,ax) and
            # re-executes it. Follow the bytes actually in memory, as the CPU does.
            add(f"if (rd8(c, c.cs, {here}) == 0x89) {{ {goto('c.cs', nxt)} }}")
        add(f"c.ip = 0x{nxt:04X}; rt_raise(c, 0x{n:02X}); return;")
    elif mn == M.HLT:
        add(f"c.ip = 0x{nxt:04X}; rt_halt(c); return;")
    else:
        raise Unsupported(MN.get(mn, mn))
    return out
