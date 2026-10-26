// GOSUB at 0824:69F3: "Eliminating Grounded Balloons." — removes the balloons that reached the ground.
//
//   CALL 19AE:198C(0, 1)                                     ' [?]
//   page flip PG&(1); boxes; IF SOUNDOFF% = 0 THEN SOUND 250, D1!
//   (font setup); CALL 17ED:0059(4, 4, 146)                  ' [?]
//   print "Eliminating Grounded" at 220,154; "Balloons.            " at 220,174
//   page flip PG&(0); CALL 17FE:010A(2)                      ' wait 2 seconds
//   CALL 19AE:198C(1, 0)
//   FOR I = 1 TO NG%                                         ' NG%: DGROUP 416; G(): columns, descriptor 404
//     IF SOUNDOFF% = 0 THEN SOUND 200, D2!
//     LINE (X(G(I)), Y(16))-(X(G(I) + 1) - 1, Y(17)), 15, BF  ' runtime 2AAB, 2AC5, 119C [?LINE]
//     CALL 17FE:010A(1)                                      ' wait 1 second
//     LINE (X(G(I)), Y(16))-(X(G(I) + 1) - 1, Y(17)), 1, BF
//     PUT (X(G(I)), Y(16)), A1, 3                            ' sprite descriptor 1EA
//     IF SOUNDOFF% = 0 THEN SOUND 300, D3!
//   NEXT
//   CALL 17FE:010A(1): CALL 1932:0054: NG% = 0
#include "common.h"

namespace {

using namespace bc;

constexpr uint16_t kI = 0x3C, kNG = 0x416, kG = 0x404, kX = 0x2F8, kY = 0x30A;

uint16_t g(Cpu& c, uint16_t v) { return rd16(c, c.ds, v); }

// `mov si,desc; add bx,[si+0Ah]; mov es,[si+2]`
void elem(Cpu& c, uint16_t desc) {
    c.si = desc; c.bx = add16(c, c.bx, g(c, uint16_t(desc + 0x0A))); c.es = g(c, uint16_t(desc + 2)); c.icount += 3;
}

// First corner (X(G(I)), Y(16)) from I: also keeps I*2 at BP-0C. Block of 20 instructions.
void corner1_from_i(Qb& q, uint16_t ret) {
    Cpu& c = q.c;
    c.bx = g(c, kI); c.bx = SHL16(c, c.bx, 1); c.icount += 2;
    c.si = kG; c.dx = c.bx; c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kG + 0x0A)); c.es = g(c, kG + 2); c.icount += 2;
    c.ax = rd16(c, c.es, c.bx); c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.icount += 3;
    elem(c, kX); push16(c, rd16(c, c.es, c.bx)); c.icount += 1;
    c.si = kY; c.bx = 0x20; c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    wr16(c, c.ss, uint16_t(c.bp - 0x0C), c.dx); c.icount += 1;
    q.callf(RT, 0x2AAB, ret);
}

// Second corner (X(G(I) + 1) - 1, Y(17)) from I*2 at BP-0C. keep_dx: the second use also copies BX to DX.
void corner2(Qb& q, bool keep_dx, uint16_t ret) {
    Cpu& c = q.c;
    c.si = kG; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0C)); c.icount += 2;
    if (keep_dx) { c.dx = c.bx; c.icount += 1; }
    c.bx = add16(c, c.bx, g(c, kG + 0x0A)); c.es = g(c, kG + 2); c.icount += 2;
    c.ax = rd16(c, c.es, c.bx); c.ax = SHL16(c, c.ax, 1); c.ax = add16(c, c.ax, 2); c.bx = c.ax; c.icount += 4;
    elem(c, kX);
    c.ax = rd16(c, c.es, c.bx); c.ax = dec16(c, c.ax); push16(c, c.ax); c.icount += 3;
    c.si = kY; c.bx = 0x22; c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    q.callf(RT, 0x2AC5, ret);
}

// First corner again from I*2 at BP-0C (for the PUT). Block of 17 instructions.
void corner1_from_temp(Qb& q, uint16_t ret) {
    Cpu& c = q.c;
    c.si = kG; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0C)); c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kG + 0x0A)); c.es = g(c, kG + 2); c.icount += 2;
    c.ax = rd16(c, c.es, c.bx); c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.icount += 3;
    elem(c, kX); push16(c, rd16(c, c.es, c.bx)); c.icount += 1;
    c.si = kY; c.bx = 0x20; c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    q.callf(RT, 0x2AAB, ret);
}

bool greater(const Cpu& c) {
    return !(c.flags & F_ZF) && (((c.flags & F_SF) != 0) == ((c.flags & F_OF) != 0));
}

void eliminate_grounded(Cpu& c) {
    Qb q{c};
    q.call(RT, 0x198C, {0, 1}, 0x69FF);
    flip(q, 1, 0x6CE, 0x6A22);
    static const uint16_t b1[8] = {12, 22, 16, 54, 0, 0, 2, 2}, b2[8] = {11, 24, 15, 56, 1, 9, 2, 2};
    box(q, 0x6D2, b1, 0x6A91);
    box(q, 0x6E2, b2, 0x6AE6);
    beep(q, 0xFA, 0x139A, 0x6AED, 0x6AF0, 0x6B01);
    static const uint16_t fr[4] = {0x6B19, 0x6B31, 0x6B3E, 0x6B53};
    font_setup(q, fr);
    q.set(0x6F2, 4); q.set(0x6F4, 4);
    q.call(0x17ED, 0x0059, {0x6F2, 0x6F4, 0x146}, 0x6B70);
    static const uint16_t r1[3] = {0x6B7D, 0x6BBE, 0x6BC7}, r2[3] = {0x6BD4, 0x6C15, 0x6C1E};
    say(q, 0x1460, 0x6F6, 0x6FA, 0xDC, 0x9A, 0x146, r1);   // "Eliminating Grounded"
    say(q, 0x1478, 0x702, 0x706, 0xDC, 0xAE, 0x146, r2);   // "Balloons.            "
    flip(q, 0, 0x70E, 0x6C40);
    q.set(0x712, 2);
    q.call(0x17FE, 0x010A, {0x712}, 0x6C68);             // wait 2 seconds
    q.call(RT, 0x198C, {1, 0}, 0x6C74);
    c.ax = g(c, kNG); wr16(c, c.ds, 0x714, c.ax); c.ax = 1; c.icount += 4;   // FOR I = 1 TO NG%
    for (;;) {
        q.at(0x6E1B);
        wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, g(c, 0x714)); c.icount += 3;
        if (greater(c)) { q.at(0x6E27); break; }
        q.at(0x6E24);
        c.icount += 1;
        q.at(0x6C80);
        beep(q, 0xC8, 0x13D8, 0x6C87, 0x6C8A, 0x6C9B);
        corner1_from_i(q, 0x6CD6);
        corner2(q, false, 0x6D0E);
        q.call(RT, 0x119C, {15, 0xFFFF, 1}, 0x6D1F);    // colour 15, box filled [?]
        q.set(0x716, 1);
        q.call(0x17FE, 0x010A, {0x716}, 0x6D2E);         // wait 1 second
        corner1_from_i(q, 0x6D69);
        corner2(q, true, 0x6DA3);
        q.call(RT, 0x119C, {1, 0xFFFF, 1}, 0x6DB4);     // colour 1
        corner1_from_temp(q, 0x6DE7);
        q.push_mem(0x1EC); q.push_mem(0x1EA); q.push_imm(0x1EA); q.push_imm(3);
        q.callf(RT, 0x15A4, 0x6DFC);                     // PUT ..., A1, 3
        beep(q, 0x12C, 0x1492, 0x6E03, 0x6E06, 0x6E17);
        c.ax = g(c, kI); c.ax = inc16(c, c.ax); c.icount += 2;   // NEXT I
    }
    q.set(0x718, 1);
    q.call(0x17FE, 0x010A, {0x718}, 0x6E36);             // wait 1 second
    q.callf(0x1932, 0x0054, 0x6E3B);
    q.set(kNG, 0);                                        // NG% = 0
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x69F3, false, &eliminate_grounded, "eliminate_grounded"};

}  // namespace
