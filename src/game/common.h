// Statement groups that recur in Balloon Challenge's main module (decompiled procedures). Each performs the
// compiled sequence exactly (registers, flags, memory, instruction count); call returns are block starts.
#pragma once
#include "qb.h"

namespace bc {

using namespace rc;
using rc::qb::Qb;

constexpr uint16_t RT = 0x19AE;          // QuickBASIC runtime segment
constexpr uint16_t kSoundOff = 0x42A;    // SOUNDOFF% (options menu)
constexpr uint16_t kPlayer = 0x3FA;      // P%: 1 red, 2 yellow
constexpr uint16_t kPages = 0x56;        // PG&(): long array descriptor (video page offsets [?])

// T& = PG&(i): CALL 1794:0282(T&): PG&(i) = T& — page flip [?] with the long element passed through a
// temporary. Element 0 is addressed with `xor bx,bx`, others with `mov bx,4*i`. The copy back stays in the
// block of the call's return point, so following statements of that block may continue directly.
inline void flip(Qb& q, uint16_t index, uint16_t tmp, uint16_t ret_call) {
    Cpu& c = q.c;
    auto addr = [&] {
        c.si = kPages;
        if (index == 0) c.bx = xor16(c, c.bx, c.bx); else c.bx = uint16_t(4 * index);
        c.bx = add16(c, c.bx, q.get(kPages + 0x0A)); c.es = q.get(kPages + 2);
        c.icount += 4;
    };
    addr();
    c.ax = rd16(c, c.es, c.bx); c.dx = rd16(c, c.es, uint16_t(c.bx + 2));
    wr16(c, c.ds, tmp, c.ax); wr16(c, c.ds, uint16_t(tmp + 2), c.dx); c.icount += 4;
    q.push_imm(tmp);
    q.callf(0x1794, 0x0282, ret_call);
    addr();
    c.ax = q.get(tmp); c.dx = q.get(uint16_t(tmp + 2));
    wr16(c, c.es, c.bx, c.ax); wr16(c, c.es, uint16_t(c.bx + 2), c.dx); c.icount += 4;
}

// CALL 1794:0224 with 8 integer variables set first (box drawing [?]).
inline void box(Qb& q, uint16_t vars, const uint16_t v[8], uint16_t ret) {
    for (int i = 0; i < 8; ++i) q.set(uint16_t(vars + 2 * i), v[i]);
    q.call(0x1794, 0x0224, {vars, uint16_t(vars + 2), uint16_t(vars + 4), uint16_t(vars + 6), uint16_t(vars + 8),
                            uint16_t(vars + 10), uint16_t(vars + 12), uint16_t(vars + 14)}, ret);
}

// IF SOUNDOFF% = 0 THEN SOUND freq, D! (single at d); `after` is the block after the statement.
inline void beep(Qb& q, uint16_t freq, uint16_t d, uint16_t jmp_ip, uint16_t then_ip, uint16_t after) {
    if (q.eq(kSoundOff, 0, jmp_ip, then_ip)) {
        q.push_imm(freq); q.push_mem(uint16_t(d + 2)); q.push_mem(d);
        q.callf(RT, 0x32F8, after);
    } else {
        q.at(after);
    }
}

// Font setup before printing: runtime 1646 on descriptors 68 and 7A [?], F$ = "balloon1", font load F$.
inline void font_setup(Qb& q, const uint16_t ret[4]) {
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x68}, ret[0]);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x7A}, ret[1]);
    q.call(RT, 0x2384, {0x1200, 0x8C}, ret[2]);
    q.call(0x1664, 0x004B, {0x68, 0x8C, 0x96, 0x94}, ret[3]);
}

// T$ = text: print T$ at (x, y) in colour descriptor `colour` with the font module, release T$.
inline void say(Qb& q, uint16_t text, uint16_t t, uint16_t xy, uint16_t x, uint16_t y, uint16_t colour, const uint16_t ret[3]) {
    q.call(RT, 0x2384, {text, t}, ret[0]);
    q.set(xy, x); q.set(uint16_t(xy + 2), y); q.set(uint16_t(xy + 4), 1); q.set(uint16_t(xy + 6), 0);
    q.call(0x16CD, 0x004B, {0x68, t, xy, uint16_t(xy + 2), colour, 0x96, uint16_t(xy + 4), uint16_t(xy + 6), 0x134}, ret[1]);
    q.call(RT, 0x27AC, {t}, ret[2]);
}

}  // namespace bc
