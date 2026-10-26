// GOSUB at 0824:4FA1: counts the score from the screen and prints it.
//
//   RED% = 0: YEL% = 0                                       ' DGROUP 492, 494
//   FOR I = 1 TO 25                                          ' columns, DGROUP 3C
//     FOR J = 1 TO 15                                        ' rows, DGROUP 3B0
//       K = POINT(X(I) + 6, Y(J) + 11)                       ' 3BA; X() 2F8, Y() 30A; runtime 10EB [?POINT]
//       IF K = 4 THEN RED% = RED% + (16 - J) * 5             ' red balloon: higher rows count more
//       IF K = 14 THEN YEL% = YEL% + (16 - J) * 5            ' yellow balloon
//     NEXT
//   NEXT
//   (runtime 1646 on 68, 7A; F$ = "balloon1"; font load)     ' as show_thinking
//   RED% = RED% + RB%: YEL% = YEL% + YB%                     ' bonuses, DGROUP 3F2, 3F4
//   CALL 17ED:0059(15, 15, 49A)                              ' [?]
//   CALL 1794:0160(10, 153, 67, 187, 8, 1, 2, 3)             ' box at the left [?]
//   T$ = STR$(RED%): print T$ at 15,159                      ' runtime 267E [?STR$], font print 16CD:004B
//   CALL 1794:0160(572, 153, 629, 187, 8, 1, 2, 3)           ' box at the right [?]
//   T$ = STR$(YEL%): print T$ at 580,159
//   RETURN
#include "qb.h"

namespace {

using namespace rc;
using rc::qb::Qb;

constexpr uint16_t RT = 0x19AE;
constexpr uint16_t kI = 0x3C, kJ = 0x3B0, kK = 0x3BA, kRed = 0x492, kYel = 0x494, kX = 0x2F8, kY = 0x30A;

bool zf(const Cpu& c) { return (c.flags & F_ZF) != 0; }
bool le(const Cpu& c) { return zf(c) || (((c.flags & F_SF) != 0) != ((c.flags & F_OF) != 0)); }
bool gt(const Cpu& c) { return !le(c); }

// SCORE = SCORE + (16 - J) * 5: `mov ax,10h; sub ax,[J]; mov cx,5; imul cx; add [score],ax`.
void add_points(Qb& q, uint16_t score) {
    Cpu& c = q.c;
    c.ax = 0x10; c.ax = sub16(c, c.ax, q.get(kJ)); c.cx = 5; imul16(c, c.cx);
    wr16(c, c.ds, score, add16(c, q.get(score), c.ax));
    c.icount += 5;
}

// print STR$(score) in a box: CALL 1794:0160 with 8 variables, T$ = STR$(score), font print, release T$.
void print_score(Qb& q, uint16_t vars, const uint16_t box[8], uint16_t score, uint16_t t, uint16_t xy,
                 uint16_t x, const uint16_t ret[6]) {
    for (int i = 0; i < 8; ++i) q.set(uint16_t(vars + 2 * i), box[i]);
    q.call(0x1794, 0x0160, {vars, uint16_t(vars + 2), uint16_t(vars + 4), uint16_t(vars + 6), uint16_t(vars + 8),
                            uint16_t(vars + 10), uint16_t(vars + 12), uint16_t(vars + 14)}, ret[0]);
    q.push_mem(score); q.callf(RT, 0x267E, ret[1]);                      // STR$(score)
    q.push_ax(); q.push_imm(t); q.callf(RT, 0x2384, ret[2]);             // T$ = ...
    q.set(xy, x); q.set(uint16_t(xy + 2), 0x9F); q.set(uint16_t(xy + 4), 1); q.set(uint16_t(xy + 6), 0);
    q.call(0x16CD, 0x004B, {0x68, t, xy, uint16_t(xy + 2), 0x49A, 0x96, uint16_t(xy + 4), uint16_t(xy + 6), 0x134}, ret[3]);
    q.call(RT, 0x27AC, {t}, ret[4]);
}

void count_score(Cpu& c) {
    Qb q{c};
    q.set(kRed, 0); q.set(kYel, 0);
    c.ax = 1; c.icount += 2;                                             // FOR I = 1 (mov ax,1; jmp test)
    for (;;) {
        q.at(0x502E);
        wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, 25); c.icount += 3;  // I > 25 ?
        if (gt(c)) { q.at(0x5039); break; }
        q.at(0x5036);
        c.icount += 1;                                                   // jmp to the body
        q.at(0x4FB4);
        c.ax = 1; c.icount += 2;                                         // FOR J = 1 (mov ax,1; jmp test)
        for (;;) {
            q.at(0x5022);
            wr16(c, c.ds, kJ, c.ax); (void)sub16(c, c.ax, 15); c.icount += 3;  // J <= 15: body
            if (!le(c)) { q.at(0x502A); break; }
            q.at(0x4FBA);
            c.bx = q.get(kI); c.bx = SHL16(c, c.bx, 1); c.si = kX; c.icount += 3;
            c.bx = add16(c, c.bx, q.get(kX + 0x0A)); c.es = q.get(kX + 2); c.icount += 2;
            c.cx = rd16(c, c.es, c.bx); c.cx = add16(c, c.cx, 6); push16(c, c.cx); c.icount += 3;   // X(I) + 6
            c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.si = kY; c.icount += 3;
            c.bx = add16(c, c.bx, q.get(kY + 0x0A)); c.es = q.get(kY + 2); c.icount += 2;
            c.ax = rd16(c, c.es, c.bx); c.ax = add16(c, c.ax, 0x0B); push16(c, c.ax); c.icount += 3; // Y(J) + 11
            q.callf(RT, 0x10EB, 0x4FE9);
            wr16(c, c.ds, kK, c.ax); (void)sub16(c, c.ax, 4); c.icount += 3;   // IF K = 4
            if (zf(c)) { q.at(0x4FF4); add_points(q, kRed); }
            else { q.at(0x4FF1); c.icount += 1; }
            q.at(0x5004);
            if (q.eq(kK, 14, 0x500B, 0x500E)) add_points(q, kYel);           // IF K = 14
            q.at(0x501E);
            c.ax = q.get(kJ); c.ax = inc16(c, c.ax); c.icount += 2;          // NEXT J
        }
        c.ax = q.get(kI); c.ax = inc16(c, c.ax); c.icount += 2;              // NEXT I
    }
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x68}, 0x5051);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x7A}, 0x5069);
    q.call(RT, 0x2384, {0x1200, 0x8C}, 0x5076);                          // F$ = "balloon1"
    q.call(0x1664, 0x004B, {0x68, 0x8C, 0x96, 0x94}, 0x508B);
    c.ax = q.get(0x3F2); wr16(c, c.ds, kRed, add16(c, q.get(kRed), c.ax)); c.icount += 2;  // RED% += RB%
    c.ax = q.get(0x3F4); wr16(c, c.ds, kYel, add16(c, q.get(kYel), c.ax)); c.icount += 2;  // YEL% += YB%
    q.set(0x496, 15); q.set(0x498, 15);
    q.call(0x17ED, 0x0059, {0x496, 0x498, 0x49A}, 0x50B6);
    static const uint16_t left[8] = {10, 0x99, 0x43, 0xBB, 8, 1, 2, 3}, right[8] = {0x23C, 0x99, 0x275, 0xBB, 8, 1, 2, 3};
    static const uint16_t rl[6] = {0x510B, 0x5114, 0x511E, 0x515F, 0x5168, 0}, rr[6] = {0x51BD, 0x51C6, 0x51D0, 0x5211, 0x521A, 0};
    print_score(q, 0x49C, left, kRed, 0x4AC, 0x4B0, 0x0F, rl);
    print_score(q, 0x4B8, right, kYel, 0x4C8, 0x4CC, 0x244, rr);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x4FA1, false, &count_score, "count_score"};

}  // namespace
