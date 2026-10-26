// GOSUB at 0824:762A: "PRESS ANYKEY TO DECIDE WHO WILL GO FIRST." — after a key, a random number of
// flashes alternates the red and yellow markers; the last one shown starts (P%, DGROUP 3FA).
//
//   CALL 19AE:198C(0, 1)                                       ' [?]
//   IF REG% = 1 THEN GOSUB SelectOpponent                      ' registered: 0824:7D32
//   page flip PG&(1); boxes; (font setup); CALL 17ED:0059(0, 0, 146): CALL 17ED:0059(1, 1, 746)   ' [?]
//   print "PRESS ANYKEY TO DECIDE" at 193,154; "WHO WILL GO FIRST." at 193,174
//   page flip PG&(0); IF SOUNDOFF% = 0 THEN SOUND 300, D!
//   CALL 1932:0054: K$ = INPUT$(1) [?runtime 2B4A (1, 7FFF)]   ' waits for a key (lockstep stop)
//   box (12, 23, 14, 57, 15, 15, 2, 2)
//   RANDOMIZE TIMER: N = <RND expression>                      ' 8087-emulator code, runtime + 21E5:00AB/0038
//   FOR I = 1 TO N                                             ' DGROUP 3C, N in 82E
//     RANDOMIZE TIMER: W = <RND expression>                    ' W in 3B4
//     IF I = 2 THEN W = 1
//     IF I = 3 THEN W = 2
//     IF W = 1 THEN PUT (X(13) - 10, Y(9) - 5), MR, 3: P% = 0  ' marker sprites 1A2 / 190
//     IF W = 2 THEN PUT (X(13) - 10, Y(9) - 5), MY, 3: P% = 1
//     IF SOUNDOFF% = 0 THEN SOUND 100, D2!
//     CALL 17FE:013A(2)                                        ' wait 2 ticks
//   NEXT
//   CALL 17FE:010A(1); box; (font setup); CALL 17ED:0059(1, 1, 746)
//   IF W = 1 THEN PUT (X(13) - 10, Y(10) - 15), MR, 3: P% = 1
//   IF W = 2 THEN PUT (X(13) - 10, Y(10) - 15), MY, 3: P% = 0
//   IF W = 1 THEN print "YELLOW WILL PLAY FIRST" at 204,150
//   IF W = 2 THEN print "RED WILL PLAY FIRST" at 220,150
//   IF SOUNDOFF% = 0 THEN SOUND 400, D3!
//   CALL 17FE:010A(2): CALL 19AE:198C(1, 0)
//   IF SOUNDOFF% = 0 THEN SOUND 50, D4!
//   CALL 1932:0054
#include "common.h"

namespace {

using namespace bc;

constexpr uint16_t kI = 0x3C, kW = 0x3B4, kX = 0x2F8, kY = 0x30A;

// (X(13) - 10, Y(row) + dy) then runtime 2AAB: 15 instructions (element offsets 1Ah and row*2).
void marker_xy(Qb& q, uint16_t row_off, uint16_t dy, uint16_t ret) {
    Cpu& c = q.c;
    c.si = kX; c.bx = 0x1A; c.icount += 2;
    c.bx = add16(c, c.bx, q.get(kX + 0x0A)); c.es = q.get(kX + 2); c.icount += 2;
    c.ax = rd16(c, c.es, c.bx); c.ax = add16(c, c.ax, 0xFFF6); push16(c, c.ax); c.icount += 3;
    c.si = kY; c.bx = row_off; c.icount += 2;
    c.bx = add16(c, c.bx, q.get(kY + 0x0A)); c.es = q.get(kY + 2); c.icount += 2;
    c.ax = rd16(c, c.es, c.bx); c.ax = add16(c, c.ax, dy); push16(c, c.ax); c.icount += 3;
    q.callf(RT, 0x2AAB, ret);
}

void put(Qb& q, uint16_t sprite, uint16_t ret) {
    q.push_mem(uint16_t(sprite + 2)); q.push_mem(sprite); q.push_imm(sprite); q.push_imm(3);
    q.callf(RT, 0x15A4, ret);
}

// RANDOMIZE TIMER and a RND expression, as compiled (a = address of the TIMER call); result in AX. The
// 8087-emulator sequences between the calls run as the original code (qb.h fpu_seq).
void randomize_rnd(Qb& q, uint16_t a) {
    q.callf(RT, 0x337C, uint16_t(a + 5));                       // TIMER
    q.fpu_seq(uint16_t(a + 5), uint16_t(a + 20));
    q.callf(RT, 0x32D5, uint16_t(a + 25));                      // RANDOMIZE
    q.callf(RT, 0x325C, uint16_t(a + 30));                      // RND
    q.fpu_seq(uint16_t(a + 30), uint16_t(a + 42));
    q.callf(0x21E5, 0x00AB, uint16_t(a + 47));
    q.fpu_seq(uint16_t(a + 47), uint16_t(a + 52));
    q.callf(0x21E5, 0x0038, uint16_t(a + 57));
}

void decide_first(Cpu& c) {
    Qb q{c};
    q.call(RT, 0x198C, {0, 1}, 0x7636);
    if (q.eq(0x10A, 1, 0x763D, 0x7640)) q.call_near(0x7D32, 0x7643);   // registered: select opponent
    else q.at(0x7643);
    flip(q, 1, 0x7D0, 0x7666);
    static const uint16_t b1[8] = {12, 20, 16, 56, 0, 0, 2, 2}, b2[8] = {11, 22, 15, 58, 9, 15, 2, 2},
                          b3[8] = {12, 23, 14, 57, 15, 15, 2, 2};
    box(q, 0x7D4, b1, 0x76D5);
    box(q, 0x7E4, b2, 0x772A);
    static const uint16_t f1[4] = {0x7742, 0x775A, 0x7767, 0x777C};
    font_setup(q, f1);
    q.set(0x7F4, 0); q.set(0x7F6, 0);
    q.call(0x17ED, 0x0059, {0x7F4, 0x7F6, 0x146}, 0x7799);
    q.set(0x7F8, 1); q.set(0x7FA, 1);
    q.call(0x17ED, 0x0059, {0x7F8, 0x7FA, 0x746}, 0x77B6);
    static const uint16_t r1[3] = {0x77C3, 0x7804, 0x780D}, r2[3] = {0x781A, 0x785B, 0x7864};
    say(q, 0x1512, 0x7FC, 0x800, 0xC1, 0x9A, 0x746, r1);       // "PRESS ANYKEY TO DECIDE"
    say(q, 0x152C, 0x808, 0x80C, 0xC1, 0xAE, 0x746, r2);       // "WHO WILL GO FIRST."
    flip(q, 0, 0x814, 0x7886);
    beep(q, 0x12C, 0x14AC, 0x78A6, 0x78A9, 0x78BA);
    q.callf(0x1932, 0x0054, 0x78BF);
    q.call(RT, 0x2B4A, {1, 0x7FFF}, 0x78CC);                   // K$ = INPUT$(1) [?]
    q.push_ax(); q.push_imm(0x818); q.callf(RT, 0x2384, 0x78D6);
    box(q, 0x81C, b3, 0x792B);
    randomize_rnd(q, 0x792B);
    wr16(c, c.ds, 0x82C, c.ax); wr16(c, c.ds, 0x82E, c.ax); c.ax = 1; c.icount += 4;   // FOR I = 1 TO N
    for (;;) {
        q.at(0x7A9A);
        wr16(c, c.ds, kI, c.ax); (void)sub16(c, c.ax, q.get(0x82E)); c.icount += 3;
        if (!(c.flags & F_ZF) && (((c.flags & F_SF) != 0) == ((c.flags & F_OF) != 0))) { q.at(0x7AA6); break; }
        q.at(0x7AA3); c.icount += 1; q.at(0x7970);
        randomize_rnd(q, 0x7970);
        wr16(c, c.ds, kW, c.ax); c.icount += 1;
        if (q.eq(kI, 2, 0x79B3, 0x79B6)) q.set(kW, 1);
        q.at(0x79BC);
        if (q.eq(kI, 3, 0x79C3, 0x79C6)) q.set(kW, 2);
        q.at(0x79CC);
        if (q.eq(kW, 1, 0x79D3, 0x79D6)) { marker_xy(q, 0x12, 0xFFFB, 0x7A01); put(q, 0x1A2, 0x7A16); q.set(kPlayer, 0); }
        q.at(0x7A1C);
        if (q.eq(kW, 2, 0x7A23, 0x7A26)) { marker_xy(q, 0x12, 0xFFFB, 0x7A51); put(q, 0x190, 0x7A66); q.set(kPlayer, 1); }
        q.at(0x7A6C);
        beep(q, 0x64, 0x1282, 0x7A73, 0x7A76, 0x7A87);
        q.set(0x830, 2);
        q.call(0x17FE, 0x013A, {0x830}, 0x7A96);               // wait 2 ticks
        c.ax = q.get(kI); c.ax = inc16(c, c.ax); c.icount += 2; // NEXT I
    }
    q.set(0x832, 1);
    q.call(0x17FE, 0x010A, {0x832}, 0x7AB5);                   // wait 1 second
    box(q, 0x834, b3, 0x7B0A);
    static const uint16_t f2[4] = {0x7B22, 0x7B3A, 0x7B47, 0x7B5C};
    font_setup(q, f2);
    q.set(0x844, 1); q.set(0x846, 1);
    q.call(0x17ED, 0x0059, {0x844, 0x846, 0x746}, 0x7B79);
    if (q.eq(kW, 1, 0x7B80, 0x7B83)) { marker_xy(q, 0x14, 0xFFF1, 0x7BAE); put(q, 0x1A2, 0x7BC3); q.set(kPlayer, 1); }
    q.at(0x7BC9);
    if (q.eq(kW, 2, 0x7BD0, 0x7BD3)) { marker_xy(q, 0x14, 0xFFF1, 0x7BFE); put(q, 0x190, 0x7C13); q.set(kPlayer, 0); }
    q.at(0x7C19);
    static const uint16_t r3[3] = {0x7C30, 0x7C71, 0x7C7A}, r4[3] = {0x7C91, 0x7CD2, 0x7CDB};
    if (q.eq(kW, 1, 0x7C20, 0x7C23)) say(q, 0x1546, 0x848, 0x84C, 0xCC, 0x96, 0x746, r3);   // "YELLOW WILL PLAY FIRST"
    else q.at(0x7C7A);
    if (q.eq(kW, 2, 0x7C81, 0x7C84)) say(q, 0x1560, 0x854, 0x858, 0xDC, 0x96, 0x746, r4);   // "RED WILL PLAY FIRST"
    else q.at(0x7CDB);
    beep(q, 0x190, 0x139A, 0x7CE2, 0x7CE5, 0x7CF6);
    q.set(0x860, 2);
    q.call(0x17FE, 0x010A, {0x860}, 0x7D05);                   // wait 2 seconds
    q.call(RT, 0x198C, {1, 0}, 0x7D11);
    beep(q, 0x32, 0x13D8, 0x7D18, 0x7D1B, 0x7D2C);
    q.callf(0x1932, 0x0054, 0x7D31);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x762A, false, &decide_first, "decide_first"};

}  // namespace
