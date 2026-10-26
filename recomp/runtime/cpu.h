// Real-mode x86 CPU state and instruction semantics for statically recompiled DOS programs.
// Semantics follow a 386 in real mode as emulated by DOSBox-X (cputype=386), the reference used for
// lockstep verification. Flags are computed eagerly; flags the Intel manual leaves undefined are set
// the way the conformance oracle (Unicorn) does and are masked out in conformance tests.
#pragma once
#include <cstdint>

namespace rc {

enum : uint16_t {
    F_CF = 0x0001, F_PF = 0x0004, F_AF = 0x0010, F_ZF = 0x0040, F_SF = 0x0080,
    F_TF = 0x0100, F_IF = 0x0200, F_DF = 0x0400, F_OF = 0x0800,
    F_ARITH = F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF,
};

struct Cpu {
    union { uint16_t ax; struct { uint8_t al, ah; }; };
    union { uint16_t cx; struct { uint8_t cl, ch; }; };
    union { uint16_t dx; struct { uint8_t dl, dh; }; };
    union { uint16_t bx; struct { uint8_t bl, bh; }; };
    uint16_t sp = 0, bp = 0, si = 0, di = 0;
    uint16_t cs = 0, ds = 0, es = 0, ss = 0;
    uint16_t ip = 0;
    uint16_t flags = 0x0002;
    uint64_t icount = 0;          // executed instructions (the clock, as DOSBox cycles=fixed)
    uint8_t* mem = nullptr;       // 1 MB + 64 KB (A20 enabled: HMA reachable)
    Cpu() : ax(0), cx(0), dx(0), bx(0) {}
};

// ---- memory (video and other device ranges are routed by the machine) ----
using MemRead = uint8_t (*)(uint32_t lin);
using MemWrite = void (*)(uint32_t lin, uint8_t v);
extern MemRead g_vram_read;       // A0000-BFFFF
extern MemWrite g_vram_write;
// Self-modifying-code detection: 1 bit per linear byte that is covered by translated code (null: off).
// A write that changes such a byte is reported to the runtime (translated code no longer matches memory).
extern uint8_t* g_code_map;
void rt_code_write(uint32_t a, uint8_t v);

inline uint32_t lin(uint16_t seg, uint16_t off) { return (uint32_t(seg) << 4) + off; }  // A20 enabled: up to 10FFEFh
inline bool is_vram(uint32_t a) { return a >= 0xA0000 && a < 0xC0000; }

// Byte access by linear address (A20 enabled, memory array is 1 MB + 64 KB).
inline uint8_t rdl8(Cpu& c, uint32_t a) { a &= 0x1FFFFF; return is_vram(a) ? g_vram_read(a) : c.mem[a]; }
inline void wrl8(Cpu& c, uint32_t a, uint8_t v) {
    a &= 0x1FFFFF;
    if (is_vram(a)) { g_vram_write(a, v); return; }
    if (g_code_map && (g_code_map[a >> 3] & (1 << (a & 7))) && c.mem[a] != v) rt_code_write(a, v);
#ifndef RC_ALL_RAM
    if (a >= 0xC0000 && a < 0x100000) return;  // ROM area: writes ignored
#endif
    c.mem[a] = v;
}
inline uint8_t rd8(Cpu& c, uint16_t seg, uint16_t off) { return rdl8(c, lin(seg, off)); }
inline void wr8(Cpu& c, uint16_t seg, uint16_t off, uint8_t v) { wrl8(c, lin(seg, off), v); }
// Word access is linear-contiguous: a word at offset FFFFh takes its high byte from the next linear
// address, not from offset 0 (DOSBox-X core and Unicorn agree; verified by conformance tests).
inline uint16_t rd16(Cpu& c, uint16_t seg, uint16_t off) {
    const uint32_t a = lin(seg, off);
    return uint16_t(rdl8(c, a) | (rdl8(c, a + 1) << 8));
}
inline void wr16(Cpu& c, uint16_t seg, uint16_t off, uint16_t v) {
    const uint32_t a = lin(seg, off);
    wrl8(c, a, uint8_t(v));
    wrl8(c, a + 1, uint8_t(v >> 8));
}
inline void push16(Cpu& c, uint16_t v) { c.sp = uint16_t(c.sp - 2); wr16(c, c.ss, c.sp, v); }
inline uint16_t pop16(Cpu& c) { uint16_t v = rd16(c, c.ss, c.sp); c.sp = uint16_t(c.sp + 2); return v; }

// ---- flags helpers ----
inline bool parity(uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return !(v & 1); }
inline void setf(Cpu& c, uint16_t mask, bool on) { c.flags = on ? (c.flags | mask) : (c.flags & ~mask); }
inline bool cf(const Cpu& c) { return c.flags & F_CF; }
inline void szp8(Cpu& c, uint8_t r) { setf(c, F_ZF, r == 0); setf(c, F_SF, r & 0x80); setf(c, F_PF, parity(r)); }
inline void szp16(Cpu& c, uint16_t r) { setf(c, F_ZF, r == 0); setf(c, F_SF, r & 0x8000); setf(c, F_PF, parity(uint8_t(r))); }

// ---- ALU (8 and 16 bit) ----
inline uint8_t add8(Cpu& c, uint8_t a, uint8_t b, int carry = 0) {
    unsigned r = unsigned(a) + b + carry;
    setf(c, F_CF, r > 0xFF); setf(c, F_AF, ((a ^ b ^ r) & 0x10)); setf(c, F_OF, (~(a ^ b) & (a ^ r)) & 0x80);
    szp8(c, uint8_t(r)); return uint8_t(r);
}
inline uint16_t add16(Cpu& c, uint16_t a, uint16_t b, int carry = 0) {
    unsigned r = unsigned(a) + b + carry;
    setf(c, F_CF, r > 0xFFFF); setf(c, F_AF, ((a ^ b ^ r) & 0x10)); setf(c, F_OF, (~(a ^ b) & (a ^ r)) & 0x8000);
    szp16(c, uint16_t(r)); return uint16_t(r);
}
inline uint8_t adc8(Cpu& c, uint8_t a, uint8_t b) { return add8(c, a, b, cf(c)); }
inline uint16_t adc16(Cpu& c, uint16_t a, uint16_t b) { return add16(c, a, b, cf(c)); }
inline uint8_t sub8(Cpu& c, uint8_t a, uint8_t b, int borrow = 0) {
    unsigned r = unsigned(a) - b - borrow;
    setf(c, F_CF, unsigned(a) < unsigned(b) + borrow); setf(c, F_AF, ((a ^ b ^ r) & 0x10));
    setf(c, F_OF, ((a ^ b) & (a ^ r)) & 0x80); szp8(c, uint8_t(r)); return uint8_t(r);
}
inline uint16_t sub16(Cpu& c, uint16_t a, uint16_t b, int borrow = 0) {
    unsigned r = unsigned(a) - b - borrow;
    setf(c, F_CF, unsigned(a) < unsigned(b) + borrow); setf(c, F_AF, ((a ^ b ^ r) & 0x10));
    setf(c, F_OF, ((a ^ b) & (a ^ r)) & 0x8000); szp16(c, uint16_t(r)); return uint16_t(r);
}
inline uint8_t sbb8(Cpu& c, uint8_t a, uint8_t b) { return sub8(c, a, b, cf(c)); }
inline uint16_t sbb16(Cpu& c, uint16_t a, uint16_t b) { return sub16(c, a, b, cf(c)); }
inline void logic_flags8(Cpu& c, uint8_t r) { setf(c, F_CF | F_OF | F_AF, false); szp8(c, r); }
inline void logic_flags16(Cpu& c, uint16_t r) { setf(c, F_CF | F_OF | F_AF, false); szp16(c, r); }
inline uint8_t and8(Cpu& c, uint8_t a, uint8_t b) { uint8_t r = a & b; logic_flags8(c, r); return r; }
inline uint16_t and16(Cpu& c, uint16_t a, uint16_t b) { uint16_t r = a & b; logic_flags16(c, r); return r; }
inline uint8_t or8(Cpu& c, uint8_t a, uint8_t b) { uint8_t r = a | b; logic_flags8(c, r); return r; }
inline uint16_t or16(Cpu& c, uint16_t a, uint16_t b) { uint16_t r = a | b; logic_flags16(c, r); return r; }
inline uint8_t xor8(Cpu& c, uint8_t a, uint8_t b) { uint8_t r = a ^ b; logic_flags8(c, r); return r; }
inline uint16_t xor16(Cpu& c, uint16_t a, uint16_t b) { uint16_t r = a ^ b; logic_flags16(c, r); return r; }
inline uint8_t inc8(Cpu& c, uint8_t a) { bool cf0 = cf(c); uint8_t r = add8(c, a, 1); setf(c, F_CF, cf0); return r; }
inline uint16_t inc16(Cpu& c, uint16_t a) { bool cf0 = cf(c); uint16_t r = add16(c, a, 1); setf(c, F_CF, cf0); return r; }
inline uint8_t dec8(Cpu& c, uint8_t a) { bool cf0 = cf(c); uint8_t r = sub8(c, a, 1); setf(c, F_CF, cf0); return r; }
inline uint16_t dec16(Cpu& c, uint16_t a) { bool cf0 = cf(c); uint16_t r = sub16(c, a, 1); setf(c, F_CF, cf0); return r; }
inline uint8_t neg8(Cpu& c, uint8_t a) { uint8_t r = sub8(c, 0, a); setf(c, F_CF, a != 0); return r; }
inline uint16_t neg16(Cpu& c, uint16_t a) { uint16_t r = sub16(c, 0, a); setf(c, F_CF, a != 0); return r; }

// ---- shifts and rotates (count masked to 5 bits as on 186+/386) ----
template <typename T, int BITS>
inline T shl_t(Cpu& c, T a, uint8_t n) {
    n &= 0x1F; if (!n) return a;
    uint32_t r = uint32_t(a) << n;
    setf(c, F_CF, (r >> BITS) & 1);
    T res = T(r); if (BITS == 8) szp8(c, uint8_t(res)); else szp16(c, uint16_t(res));
    setf(c, F_OF, ((res >> (BITS - 1)) & 1) ^ cf(c)); setf(c, F_AF, false); return res;
}
template <typename T, int BITS>
inline T shr_t(Cpu& c, T a, uint8_t n) {
    n &= 0x1F; if (!n) return a;
    setf(c, F_CF, n <= BITS ? (a >> (n - 1)) & 1 : 0);
    T res = n >= BITS ? 0 : T(a >> n); if (BITS == 8) szp8(c, uint8_t(res)); else szp16(c, uint16_t(res));
    setf(c, F_OF, (a >> (BITS - 1)) & 1); setf(c, F_AF, false); return res;
}
template <typename T, int BITS, typename S>
inline T sar_t(Cpu& c, T a, uint8_t n) {
    n &= 0x1F; if (!n) return a;
    S s = S(a);
    T res; bool cfo;
    if (n < BITS) { cfo = (s >> (n - 1)) & 1; res = T(s >> n); }
    else { cfo = s < 0; res = T(s < 0 ? -1 : 0); }
    setf(c, F_CF, cfo);
    if (BITS == 8) szp8(c, uint8_t(res)); else szp16(c, uint16_t(res));
    setf(c, F_OF, false); setf(c, F_AF, false); return res;
}
template <typename T, int BITS>
inline T rol_t(Cpu& c, T a, uint8_t n) {
    if (!(n & 0x1F)) return a; n = n % BITS;
    T res = n ? T((a << n) | (a >> (BITS - n))) : a;
    setf(c, F_CF, res & 1); setf(c, F_OF, ((res >> (BITS - 1)) ^ res) & 1); return res;
}
template <typename T, int BITS>
inline T ror_t(Cpu& c, T a, uint8_t n) {
    if (!(n & 0x1F)) return a; n = n % BITS;
    T res = n ? T((a >> n) | (a << (BITS - n))) : a;
    setf(c, F_CF, (res >> (BITS - 1)) & 1); setf(c, F_OF, ((res >> (BITS - 1)) ^ (res >> (BITS - 2))) & 1); return res;
}
template <typename T, int BITS>
inline T rcl_t(Cpu& c, T a, uint8_t n) {
    n = uint8_t((n & 0x1F) % (BITS + 1)); if (!n) return a;
    uint32_t v = (uint32_t(a) | (uint32_t(cf(c)) << BITS));
    for (int i = 0; i < n; ++i) v = ((v << 1) | (v >> BITS)) & ((1u << (BITS + 1)) - 1);
    T res = T(v); setf(c, F_CF, (v >> BITS) & 1); setf(c, F_OF, ((res >> (BITS - 1)) & 1) ^ cf(c)); return res;
}
template <typename T, int BITS>
inline T rcr_t(Cpu& c, T a, uint8_t n) {
    n = uint8_t((n & 0x1F) % (BITS + 1)); if (!n) return a;
    uint32_t v = (uint32_t(a) | (uint32_t(cf(c)) << BITS));
    for (int i = 0; i < n; ++i) v = ((v >> 1) | ((v & 1) << BITS)) & ((1u << (BITS + 1)) - 1);
    T res = T(v); setf(c, F_CF, (v >> BITS) & 1); setf(c, F_OF, ((res >> (BITS - 1)) ^ (res >> (BITS - 2))) & 1); return res;
}
#define SHL8 shl_t<uint8_t, 8>
#define SHL16 shl_t<uint16_t, 16>
#define SHR8 shr_t<uint8_t, 8>
#define SHR16 shr_t<uint16_t, 16>
#define SAR8 sar_t<uint8_t, 8, int8_t>
#define SAR16 sar_t<uint16_t, 16, int16_t>
#define ROL8 rol_t<uint8_t, 8>
#define ROL16 rol_t<uint16_t, 16>
#define ROR8 ror_t<uint8_t, 8>
#define ROR16 ror_t<uint16_t, 16>
#define RCL8 rcl_t<uint8_t, 8>
#define RCL16 rcl_t<uint16_t, 16>
#define RCR8 rcr_t<uint8_t, 8>
#define RCR16 rcr_t<uint16_t, 16>

// ---- multiply / divide (return false on divide error -> INT 0) ----
inline void mul8(Cpu& c, uint8_t b) { c.ax = uint16_t(c.al * b); bool hi = c.ah != 0; setf(c, F_CF | F_OF, hi); szp8(c, c.al); }
inline void mul16(Cpu& c, uint16_t b) {
    uint32_t r = uint32_t(c.ax) * b; c.ax = uint16_t(r); c.dx = uint16_t(r >> 16);
    setf(c, F_CF | F_OF, c.dx != 0); szp16(c, c.ax);
}
inline void imul8(Cpu& c, uint8_t b) {
    int16_t r = int16_t(int8_t(c.al) * int8_t(b)); c.ax = uint16_t(r);
    setf(c, F_CF | F_OF, r != int8_t(r)); szp8(c, c.al);
}
inline void imul16(Cpu& c, uint16_t b) {
    int32_t r = int32_t(int16_t(c.ax)) * int16_t(b); c.ax = uint16_t(r); c.dx = uint16_t(uint32_t(r) >> 16);
    setf(c, F_CF | F_OF, r != int16_t(r)); szp16(c, c.ax);
}
inline uint16_t imul16_3(Cpu& c, uint16_t a, uint16_t b) {  // 186+ IMUL r16, r/m16, imm
    int32_t r = int32_t(int16_t(a)) * int16_t(b);
    setf(c, F_CF | F_OF, r != int16_t(r)); szp16(c, uint16_t(r)); return uint16_t(r);
}
inline bool div8(Cpu& c, uint8_t b) {
    if (!b) return false; unsigned q = c.ax / b; if (q > 0xFF) return false;
    c.ah = uint8_t(c.ax % b); c.al = uint8_t(q); return true;
}
inline bool div16(Cpu& c, uint16_t b) {
    if (!b) return false; uint32_t n = (uint32_t(c.dx) << 16) | c.ax; uint32_t q = n / b; if (q > 0xFFFF) return false;
    c.dx = uint16_t(n % b); c.ax = uint16_t(q); return true;
}
inline bool idiv8(Cpu& c, uint8_t b) {
    if (!b) return false; int n = int16_t(c.ax); int q = n / int8_t(b); if (q > 127 || q < -128) return false;
    c.ah = uint8_t(n % int8_t(b)); c.al = uint8_t(q); return true;
}
inline bool idiv16(Cpu& c, uint16_t b) {
    if (!b) return false; int32_t n = int32_t((uint32_t(c.dx) << 16) | c.ax); int32_t q = n / int16_t(b);
    if (q > 32767 || q < -32768) return false;
    c.dx = uint16_t(n % int16_t(b)); c.ax = uint16_t(q); return true;
}

// ---- BCD ----
inline void daa(Cpu& c) {
    uint8_t al = c.al; bool cfo = cf(c);
    if ((al & 0xF) > 9 || (c.flags & F_AF)) { c.al = uint8_t(c.al + 6); setf(c, F_AF, true); } else setf(c, F_AF, false);
    if (al > 0x99 || cfo) { c.al = uint8_t(c.al + 0x60); setf(c, F_CF, true); } else setf(c, F_CF, false);
    szp8(c, c.al);
}
inline void das(Cpu& c) {
    uint8_t al = c.al; bool cfo = cf(c);
    if ((al & 0xF) > 9 || (c.flags & F_AF)) { c.al = uint8_t(c.al - 6); setf(c, F_AF, true); } else setf(c, F_AF, false);
    if (al > 0x99 || cfo) { c.al = uint8_t(c.al - 0x60); setf(c, F_CF, true); } else setf(c, F_CF, false);
    szp8(c, c.al);
}
inline void aaa(Cpu& c) {
    if ((c.al & 0xF) > 9 || (c.flags & F_AF)) { c.ax = uint16_t(c.ax + 0x106); setf(c, F_AF | F_CF, true); }
    else setf(c, F_AF | F_CF, false);
    c.al &= 0x0F;
}
inline bool aam(Cpu& c, uint8_t base) { if (!base) return false; c.ah = uint8_t(c.al / base); c.al = uint8_t(c.al % base); szp8(c, c.al); return true; }
inline void aad(Cpu& c, uint8_t base) { c.al = uint8_t(c.al + c.ah * base); c.ah = 0; szp8(c, c.al); }

// ---- string instructions (destination ES:DI; source segment may be overridden) ----
enum RepMode { ONCE, REP, REP_NE };
void rt_out8(Cpu& c, uint16_t port, uint8_t v);   // machine.cpp
uint8_t rt_in8(Cpu& c, uint16_t port);           // machine.cpp
inline int sdelta(const Cpu& c, int size) { return (c.flags & F_DF) ? -size : size; }
// Clock: one count per iteration, and the reference's interruptible REP (DOSBox-X "interruptible rep string
// op" = 4): the instruction runs at most kRepChunk iterations, then restarts while work remains, and each
// restart counts as another executed instruction. Without a prefix the single iteration counts too, as in the
// reference core (the instruction and its one iteration).
constexpr unsigned kRepChunk = 4;
#define RC_STR_LOOP(BODY)                                                   \
    if (mode == ONCE) { BODY; c.icount++; return; }                         \
    for (unsigned n = 1; c.cx; ++n) { BODY; c.cx = uint16_t(c.cx - 1); c.icount++; \
        if (c.cx && n % kRepChunk == 0) c.icount++; }
#define RC_STR_LOOP_CMP(BODY)                                               \
    if (mode == ONCE) { BODY; c.icount++; return; }                         \
    for (unsigned n = 1; c.cx; ++n) { BODY; c.cx = uint16_t(c.cx - 1); c.icount++; \
        bool z = c.flags & F_ZF; if ((mode == REP && !z) || (mode == REP_NE && z)) break; \
        if (c.cx && n % kRepChunk == 0) c.icount++; }
inline void str_movsb(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP(wr8(c, c.es, c.di, rd8(c, s, c.si)); c.si = uint16_t(c.si + sdelta(c, 1)); c.di = uint16_t(c.di + sdelta(c, 1)))
}
inline void str_movsw(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP(wr16(c, c.es, c.di, rd16(c, s, c.si)); c.si = uint16_t(c.si + sdelta(c, 2)); c.di = uint16_t(c.di + sdelta(c, 2)))
}
inline void str_stosb(Cpu& c, uint16_t, RepMode mode) {
    RC_STR_LOOP(wr8(c, c.es, c.di, c.al); c.di = uint16_t(c.di + sdelta(c, 1)))
}
inline void str_stosw(Cpu& c, uint16_t, RepMode mode) {
    RC_STR_LOOP(wr16(c, c.es, c.di, c.ax); c.di = uint16_t(c.di + sdelta(c, 2)))
}
inline void str_lodsb(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP(c.al = rd8(c, s, c.si); c.si = uint16_t(c.si + sdelta(c, 1)))
}
inline void str_lodsw(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP(c.ax = rd16(c, s, c.si); c.si = uint16_t(c.si + sdelta(c, 2)))
}
inline void str_scasb(Cpu& c, uint16_t, RepMode mode) {
    RC_STR_LOOP_CMP((void)sub8(c, c.al, rd8(c, c.es, c.di)); c.di = uint16_t(c.di + sdelta(c, 1)))
}
inline void str_scasw(Cpu& c, uint16_t, RepMode mode) {
    RC_STR_LOOP_CMP((void)sub16(c, c.ax, rd16(c, c.es, c.di)); c.di = uint16_t(c.di + sdelta(c, 2)))
}
inline void str_cmpsb(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP_CMP((void)sub8(c, rd8(c, s, c.si), rd8(c, c.es, c.di)); c.si = uint16_t(c.si + sdelta(c, 1)); c.di = uint16_t(c.di + sdelta(c, 1)))
}
inline void str_cmpsw(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP_CMP((void)sub16(c, rd16(c, s, c.si), rd16(c, c.es, c.di)); c.si = uint16_t(c.si + sdelta(c, 2)); c.di = uint16_t(c.di + sdelta(c, 2)))
}
inline void str_outsb(Cpu& c, uint16_t s, RepMode mode) {
    RC_STR_LOOP(rt_out8(c, c.dx, rd8(c, s, c.si)); c.si = uint16_t(c.si + sdelta(c, 1)))
}
inline void str_insb(Cpu& c, uint16_t, RepMode mode) {
    RC_STR_LOOP(wr8(c, c.es, c.di, rt_in8(c, c.dx)); c.di = uint16_t(c.di + sdelta(c, 1)))
}

// ---- flags register ----
inline uint16_t get_flags(const Cpu& c) { return uint16_t((c.flags & 0x7FD5) | 0x0002); }
inline void set_flags(Cpu& c, uint16_t v) { c.flags = uint16_t((v & 0x7FD5) | 0x0002); }

}  // namespace rc
