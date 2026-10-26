// GOSUB at 0824:5F1F: "Quit Balloon Challenge ? (Y/N)"; Y sets QUIT% (DGROUP 428) = 1.
//
//   CALL 19AE:198C(0, 1)                                    ' [?]
//   T& = PG&(1): CALL 1794:0282(T&): PG&(1) = T&            ' page flip [?]
//   CALL 1794:0224(12, 22, 16, 54, 0, 0, 2, 2)              ' box [?]
//   CALL 1794:0224(11, 24, 15, 56, 15, 7, 2, 2)             ' inner box [?]
//   (font setup)
//   CALL 17ED:0059(0, 0, 146): CALL 17ED:0059(14, 14, 5CC)  ' [?]
//   print "Quit Balloon Challenge ?" at 214,155 (colour 146), again at 213,154 (colour 5CC)   ' shadowed
//   print " (Y/N)" at 285,175 (146), again at 284,174 (5CC)
//   T& = PG&(0): CALL 1794:0282(T&): PG&(0) = T&
//   IF SOUNDOFF% = 0 THEN SOUND 250, D1!
//   K$ = "YyNn": R$ = "X": CALL 1932:005A(K$, R$)           ' wait for Y/y/N/n (lockstep stop)
//   R$ = UCASE$(R$)                                         ' [?]
//   IF R$ = "Y" THEN QUIT% = 1: RETURN                      ' literal 13D2
//   CALL 19AE:198C(1, 0)
//   IF SOUNDOFF% = 0 THEN SOUND 50, D2!
//   RETURN
#include "common.h"

namespace {

using namespace bc;

void confirm_quit(Cpu& c) {
    Qb q{c};
    q.call(RT, 0x198C, {0, 1}, 0x5F2B);
    flip(q, 1, 0x5A0, 0x5F4E);
    static const uint16_t b1[8] = {12, 22, 16, 54, 0, 0, 2, 2}, b2[8] = {11, 24, 15, 56, 15, 7, 2, 2};
    box(q, 0x5A4, b1, 0x5FBD);
    box(q, 0x5B4, b2, 0x6012);
    static const uint16_t fr[4] = {0x602A, 0x6042, 0x604F, 0x6064};
    font_setup(q, fr);
    q.set(0x5C4, 0); q.set(0x5C6, 0);
    q.call(0x17ED, 0x0059, {0x5C4, 0x5C6, 0x146}, 0x6081);
    q.set(0x5C8, 14); q.set(0x5CA, 14);
    q.call(0x17ED, 0x0059, {0x5C8, 0x5CA, 0x5CC}, 0x609E);
    static const uint16_t r1[3] = {0x60AB, 0x60EC, 0x60F5}, r2[3] = {0x6102, 0x6143, 0x614C},
                          r3[3] = {0x6159, 0x619A, 0x61A3}, r4[3] = {0x61B0, 0x61F1, 0x61FA};
    say(q, 0x139E, 0x5CE, 0x5D2, 0xD6, 0x9B, 0x146, r1);   // "Quit Balloon Challenge ?"
    say(q, 0x139E, 0x5DA, 0x5DE, 0xD5, 0x9A, 0x5CC, r2);
    say(q, 0x13BA, 0x5E6, 0x5EA, 0x11D, 0xAF, 0x146, r3);  // " (Y/N)"
    say(q, 0x13BA, 0x5F2, 0x5F6, 0x11C, 0xAE, 0x5CC, r4);
    flip(q, 0, 0x5FE, 0x621C);
    beep(q, 0xFA, 0x139A, 0x623C, 0x623F, 0x6250);
    q.call(RT, 0x2384, {0x13C4, 0x602}, 0x625D);          // K$ = "YyNn"
    q.call(RT, 0x2384, {0x13CC, 0x606}, 0x626A);          // R$ = "X"
    q.call(0x1932, 0x005A, {0x602, 0x606}, 0x6277);      // wait for a key in K$ -> R$
    q.call(0x1946, 0x003E, {0x606}, 0x6280);             // R$ = UCASE$(R$) [?]
    q.call(RT, 0x23FA, {0x606, 0x13D2}, 0x628D);         // R$ = "Y" ?
    if (q.je(0x628F, 0x6292)) {
        q.set(0x428, 1);
        q.ret_near();
        return;
    }
    q.at(0x6299);
    q.call(RT, 0x198C, {1, 0}, 0x62A5);
    beep(q, 0x32, 0x13D8, 0x62AC, 0x62AF, 0x62C0);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x5F1F, false, &confirm_quit, "confirm_quit"};

}  // namespace
