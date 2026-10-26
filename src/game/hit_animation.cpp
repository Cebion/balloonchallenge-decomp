// GOSUB at 0824:5B29: animation for the balloon at column C% (DGROUP 41A), row R% (41C) of player P%.
//
//   IF SOUNDOFF% = 0 THEN FOR F = 600 TO 50 STEP -10: SOUND F, D1!: NEXT      ' F in DGROUP 3C
//   FOR I = 1 TO 8                                                           ' flash
//     IF P% = 2 THEN PUT (X(C%), Y(R%)), Y1, 3                               ' sprites 29E/2B0 (yellow),
//     IF P% = 1 THEN PUT (X(C%), Y(R%)), R1, 3                               '         28C/2C2 (red)
//     CALL 17FE:013A(1)                                                      ' wait 1 tick
//     IF SOUNDOFF% = 0 THEN SOUND 75, D2!
//     IF P% = 2 THEN PUT (X(C%), Y(R%)), Y2, 3
//     IF P% = 1 THEN PUT (X(C%), Y(R%)), R2, 3
//     CALL 17FE:013A(1)
//   NEXT
//   CALL 17FE:010A(1): PUT (X(C%), Y(R%)), A1, 3                             ' wait 1 s; clear the cell
//   FOR J = R% + 1 TO 16                                                     ' the balloon falls
//     K = POINT(X(C%) + 6, Y(J) + 11)
//     IF K = 1 THEN                                                          ' empty sky below
//       K = POINT(X(C%) + 6, Y(J - 1) + 11)
//       IF K = 4 OR K = 14 THEN PUT (X(C%), Y(J - 1)), A1, 3                ' clear the cell above
//       IF P% = 2 THEN PUT (X(C%), Y(J)), Y1, 3
//       IF P% = 1 THEN PUT (X(C%), Y(J)), R1, 3
//       CALL 17FE:013A(2): IF J = 16 THEN CALL 17FE:013A(10)
//       PUT (X(C%), Y(J)), A1, 3
//       IF J = 16 THEN CALL 17FE:013A(10)
//     END IF
//     IF SOUNDOFF% = 0 THEN SOUND 100, D3!
//   NEXT
#include "common.h"

namespace {

using namespace bc;

constexpr uint16_t kI = 0x3C, kK = 0x3BA, kCol = 0x41A, kRow = 0x41C, kX = 0x2F8, kY = 0x30A;

uint16_t g(Cpu& c, uint16_t v) { return rd16(c, c.ds, v); }
bool zf(const Cpu& c) { return (c.flags & F_ZF) != 0; }
bool lt(const Cpu& c) { return ((c.flags & F_SF) != 0) != ((c.flags & F_OF) != 0); }
bool gt(const Cpu& c) { return !zf(c) && !lt(c); }

// (X(C%), Y(row)) from two integer variables, then runtime 2AAB. 13 instructions.
void cell(Qb& q, uint16_t row, uint16_t ret) {
    Cpu& c = q.c;
    c.bx = g(c, kCol); c.bx = SHL16(c, c.bx, 1); c.si = kX; c.icount += 3;
    c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    c.bx = g(c, row); c.bx = SHL16(c, c.bx, 1); c.si = kY; c.icount += 3;
    c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    q.callf(RT, 0x2AAB, ret);
}

// PUT ..., sprite, 3 (runtime 15A4). 7 instructions.
void put(Qb& q, uint16_t sprite, uint16_t ret) {
    q.push_mem(uint16_t(sprite + 2)); q.push_mem(sprite); q.push_imm(sprite); q.push_imm(3);
    q.callf(RT, 0x15A4, ret);
}

// IF P% = p THEN PUT (X(C%), Y(row)), sprite, 3 — `after` is the block after the statement.
void put_if_player(Qb& q, uint16_t p, uint16_t row, uint16_t sprite, uint16_t jmp_ip, uint16_t then_ip,
                   uint16_t ret_cell, uint16_t after) {
    if (q.eq(kPlayer, p, jmp_ip, then_ip)) { cell(q, row, ret_cell); put(q, sprite, after); }
    else q.at(after);
}

// CALL 17FE:013A(n) with the count in variable v: wait n timer ticks.
void wait_ticks(Qb& q, uint16_t v, uint16_t n, uint16_t ret) { q.set(v, n); q.call(0x17FE, 0x013A, {v}, ret); }

void hit_animation(Cpu& c) {
    Qb q{c};
    // IF SOUNDOFF% = 0 THEN FOR F = 600 TO 50 STEP -10: SOUND F, D1!: NEXT
    if (q.eq(kSoundOff, 0, 0x5B30, 0x5B33)) {
        c.ax = 600; c.icount += 2;
        for (;;) {
            q.at(0x5B4E);
            wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, 50); c.icount += 3;
            if (lt(c)) break;                                    // jge: F >= 50 continues
            q.at(0x5B3A);
            q.push_ax(); q.push_mem(0x1284); q.push_mem(0x1282);
            q.callf(RT, 0x32F8, 0x5B48);
            c.ax = g(c, kI); c.ax = add16(c, c.ax, 0xFFF6); c.icount += 2;   // F - 10
        }
    }
    q.at(0x5B56);
    c.ax = 1; c.icount += 2;                                     // FOR I = 1 TO 8
    for (;;) {
        q.at(0x5CB9);
        wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, 8); c.icount += 3;
        if (gt(c)) { q.at(0x5CC4); break; }
        q.at(0x5CC1); c.icount += 1; q.at(0x5B5C);
        put_if_player(q, 2, kRow, 0x29E, 0x5B63, 0x5B66, 0x5B8F, 0x5BA4);
        put_if_player(q, 1, kRow, 0x28C, 0x5BAB, 0x5BAE, 0x5BD7, 0x5BEC);
        wait_ticks(q, 0x594, 1, 0x5BFB);
        beep(q, 0x4B, 0x127E, 0x5C02, 0x5C05, 0x5C16);
        put_if_player(q, 2, kRow, 0x2B0, 0x5C1D, 0x5C20, 0x5C49, 0x5C5E);
        put_if_player(q, 1, kRow, 0x2C2, 0x5C65, 0x5C68, 0x5C91, 0x5CA6);
        wait_ticks(q, 0x596, 1, 0x5CB5);
        c.ax = g(c, kI); c.ax = inc16(c, c.ax); c.icount += 2;   // NEXT I
    }
    q.set(0x598, 1);
    q.call(0x17FE, 0x010A, {0x598}, 0x5CD3);                     // wait 1 second
    cell(q, kRow, 0x5CFC);
    put(q, 0x1EA, 0x5D11);
    c.ax = g(c, kRow); c.ax = inc16(c, c.ax); c.icount += 3;     // FOR J = R% + 1 TO 16
    for (;;) {
        q.at(0x5F13);
        wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, 16); c.icount += 3;
        if (gt(c)) { q.at(0x5F1E); break; }
        q.at(0x5F1B); c.icount += 1; q.at(0x5D18);
        // K = POINT(X(C%) + 6, Y(J) + 11), with C%*2 and J*2 kept at BP-0E and BP-0C
        c.bx = g(c, kCol); c.bx = SHL16(c, c.bx, 1); c.si = kX; c.dx = c.bx; c.icount += 4;
        c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); c.icount += 2;
        c.cx = rd16(c, c.es, c.bx); c.cx = add16(c, c.cx, 6); push16(c, c.cx); c.icount += 3;
        c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.si = kY; c.icount += 3;
        c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); c.icount += 2;
        c.cx = rd16(c, c.es, c.bx); c.cx = add16(c, c.cx, 0x0B); push16(c, c.cx); c.icount += 3;
        wr16(c, c.ss, uint16_t(c.bp - 0x0C), c.ax); wr16(c, c.ss, uint16_t(c.bp - 0x0E), c.dx); c.icount += 2;
        q.callf(RT, 0x10EB, 0x5D4F);
        wr16(c, c.ds, kK, c.ax); (void)sub16(c, c.ax, 1); c.icount += 3;
        if (zf(c)) {                                             // IF K = 1 THEN
            q.at(0x5D5A);
            // K = POINT(X(C%) + 6, Y(J - 1) + 11); (J-1)*2 kept at BP-10
            c.si = kX; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0E)); c.dx = c.bx; c.icount += 3;
            c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); c.icount += 2;
            c.ax = rd16(c, c.es, c.bx); c.ax = add16(c, c.ax, 6); push16(c, c.ax); c.icount += 3;
            c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0C)); c.bx = add16(c, c.bx, 0xFFFE); c.si = kY; c.cx = c.bx; c.icount += 4;
            c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); c.icount += 2;
            c.ax = rd16(c, c.es, c.bx); c.ax = add16(c, c.ax, 0x0B); push16(c, c.ax); c.icount += 3;
            wr16(c, c.ss, uint16_t(c.bp - 0x10), c.cx); c.icount += 1;
            q.callf(RT, 0x10EB, 0x5D8F);
            // IF K = 4 OR K = 14 (QuickBASIC booleans: -1/0 in CX and DX, then OR)
            wr16(c, c.ds, kK, c.ax); (void)sub16(c, c.ax, 4); c.cx = 0; c.icount += 4;
            if (zf(c)) { q.at(0x5D9A); c.cx = dec16(c, c.cx); c.icount += 1; }
            q.at(0x5D9B);
            (void)sub16(c, c.ax, 0x0E); c.dx = 0; c.icount += 3;
            if (zf(c)) { q.at(0x5DA3); c.dx = dec16(c, c.dx); c.icount += 1; }
            q.at(0x5DA4);
            c.dx = or16(c, c.dx, c.cx); c.dx = and16(c, c.dx, c.dx); c.icount += 3;
            if (!zf(c)) {                                        // PUT (X(C%), Y(J - 1)), A1, 3
                q.at(0x5DAD);
                c.si = kX; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0E)); c.icount += 2;
                c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
                c.si = kY; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x10)); c.icount += 2;
                c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
                q.callf(RT, 0x2AAB, 0x5DD0);
                put(q, 0x1EA, 0x5DE5);
            } else {
                q.at(0x5DAA); c.icount += 1; q.at(0x5DE5);
            }
            put_if_player(q, 2, kI, 0x29E, 0x5DEC, 0x5DEF, 0x5E18, 0x5E2D);
            put_if_player(q, 1, kI, 0x28C, 0x5E34, 0x5E37, 0x5E60, 0x5E75);
            wait_ticks(q, 0x59A, 2, 0x5E84);
            if (q.eq(kI, 16, 0x5E8B, 0x5E8E)) wait_ticks(q, 0x59C, 10, 0x5E9D);
            else q.at(0x5E9D);
            cell(q, kI, 0x5EC6);
            put(q, 0x1EA, 0x5EDB);
            if (q.eq(kI, 16, 0x5EE2, 0x5EE5)) wait_ticks(q, 0x59E, 10, 0x5EF4);
            else q.at(0x5EF4);
        } else {
            q.at(0x5D57); c.icount += 1; q.at(0x5EF4);
        }
        beep(q, 0x64, 0x139A, 0x5EFB, 0x5EFE, 0x5F0F);
        c.ax = g(c, kI); c.ax = inc16(c, c.ax); c.icount += 2;   // NEXT J
    }
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x5B29, false, &hit_animation, "hit_animation"};

}  // namespace
