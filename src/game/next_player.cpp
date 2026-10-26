// GOSUB at 0824:667B: the current player (P%, DGROUP 3FA) has no balloons left; announce that the other
// player continues.
//
//   CALL 19AE:198C(0, 1)                                    ' [?] (the same call with (1, 0) at the end)
//   T& = PG&(1): CALL 1794:0282(T&): PG&(1) = T&            ' PG&() long array, descriptor 56; page flip [?]
//   CALL 1794:0224(12, 22, 16, 54, 0, 0, 2, 2)              ' box [?]
//   CALL 1794:0224(11, 24, 15, 56, 1, 9, 2, 2)              ' inner box [?]
//   IF SOUNDOFF% = 0 THEN SOUND 250, D1!                    ' 42A; single at 139A [?]
//   (runtime 1646 on 68, 7A; F$ = "balloon1"; font load)    ' as show_thinking
//   CALL 17ED:0059(4, 4, 146)                               ' [?]
//   IF P% = 1 THEN print "Red Has No More Balloons." at 200,154
//   IF P% = 1 THEN print "Yellow Will Now Play." at 200,174
//   IF P% = 2 THEN print "Yellow Has No More Balloons." at 195,154
//   IF P% = 2 THEN print "Red Will Now Play." at 195,174
//   T& = PG&(0): CALL 1794:0282(T&): PG&(0) = T&
//   CALL 17FE:010A(3)                                       ' wait 3 seconds
//   CALL 19AE:198C(1, 0)
//   IF SOUNDOFF% = 0 THEN SOUND 50, D2!                     ' single at 13D8 [?]
//   CALL 1932:0054                                          ' keyboard flush [?]
#include "common.h"

namespace {

using namespace bc;

void next_player(Cpu& c) {
    Qb q{c};
    q.call(RT, 0x198C, {0, 1}, 0x6687);
    // the block at 66AA continues straight into the first box (one block in the original)
    flip(q, 1, 0x670, 0x66AA);
    static const uint16_t b1[8] = {12, 22, 16, 54, 0, 0, 2, 2}, b2[8] = {11, 24, 15, 56, 1, 9, 2, 2};
    box(q, 0x674, b1, 0x6719);
    box(q, 0x684, b2, 0x676E);
    beep(q, 0xFA, 0x139A, 0x6775, 0x6778, 0x6789);
    static const uint16_t fr[4] = {0x67A1, 0x67B9, 0x67C6, 0x67DB};
    font_setup(q, fr);
    q.set(0x694, 4); q.set(0x696, 4);
    q.call(0x17ED, 0x0059, {0x694, 0x696, 0x146}, 0x67F8);
    static const uint16_t r1[3] = {0x680F, 0x6850, 0x6859}, r2[3] = {0x6870, 0x68B1, 0x68BA},
                          r3[3] = {0x68D1, 0x6912, 0x691B}, r4[3] = {0x6932, 0x6973, 0x697C};
    if (q.eq(kPlayer, 1, 0x67FF, 0x6802)) say(q, 0x13F2, 0x698, 0x69C, 200, 0x9A, 0x146, r1);   // "Red Has No More Balloons."
    else q.at(0x6859);
    if (q.eq(kPlayer, 1, 0x6860, 0x6863)) say(q, 0x1410, 0x6A4, 0x6A8, 200, 0xAE, 0x146, r2);   // "Yellow Will Now Play."
    else q.at(0x68BA);
    if (q.eq(kPlayer, 2, 0x68C1, 0x68C4)) say(q, 0x142A, 0x6B0, 0x6B4, 195, 0x9A, 0x146, r3);   // "Yellow Has No More Balloons."
    else q.at(0x691B);
    if (q.eq(kPlayer, 2, 0x6922, 0x6925)) say(q, 0x144A, 0x6BC, 0x6C0, 195, 0xAE, 0x146, r4);   // "Red Will Now Play."
    else q.at(0x697C);
    flip(q, 0, 0x6C8, 0x699E);
    q.set(0x6CC, 3);
    q.call(0x17FE, 0x010A, {0x6CC}, 0x69C6);             // wait 3 seconds
    q.call(RT, 0x198C, {1, 0}, 0x69D2);
    beep(q, 0x32, 0x13D8, 0x69D9, 0x69DC, 0x69ED);
    q.callf(0x1932, 0x0054, 0x69F2);
    q.ret_near();
}

// Drafted: no recorded session reaches it yet (a player running out of balloons).
rc::NativeReg reg{0x0824, 0x667B, false, &next_player, "next_player", /*verified=*/false};

}  // namespace
