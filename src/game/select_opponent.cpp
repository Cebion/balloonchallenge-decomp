// GOSUB at 0824:7D32 (registered version, before a new game): "SELECT OPPONENT TO PLAY" box with
// <C>omputer / <H>uman, waits for C/c/H/h; COMPUTER% (DGROUP 3FC) = 1 for C, else 0.
//
//   CALL 1794:0224(12, 20, 16, 56, 0, 0, 2, 2)              ' box [?]
//   CALL 1794:0224(11, 22, 15, 58, 9, 15, 2, 2)             ' inner box [?]
//   (runtime 1646 on descriptors 68, 7A; F$ = "balloon1"; font module load F$)   ' as show_thinking
//   CALL 17ED:0059(0, 0, 146): CALL 17ED:0059(0, 4, 746)    ' [?]
//   T$ = "SELECT OPPONENT TO PLAY": print T$ at 190,154     ' font print 16CD:004B
//   T$ = "   <" + E$ + "C" + E$ + ">omputer    <" + E$ + "H" + E$ + ">uman"   ' E$: DGROUP 12C4 [?]
//   print T$ at 190,174
//   IF SOUNDOFF% = 0 THEN SOUND 300, D!                     ' 42A flag, D! single at 14AC [?]
//   CALL 1932:0054                                          ' keyboard flush [?]
//   K$ = "CcHh": R$ = "X": CALL 1932:005A(K$, R$)           ' wait for a key in K$ (lockstep stop)
//   R$ = UCASE$(R$)                                         ' 1946:003E [?]
//   COMPUTER% = (R$ = "C")
#include "qb.h"

namespace {

using rc::qb::Qb;

constexpr uint16_t RT = 0x19AE;

void select_opponent(rc::Cpu& c) {
    Qb q{c};
    const uint16_t box1[8] = {12, 20, 16, 56, 0, 0, 2, 2}, box2[8] = {11, 22, 15, 58, 9, 15, 2, 2};
    for (int i = 0; i < 8; ++i) q.set(uint16_t(0x862 + 2 * i), box1[i]);
    q.call(0x1794, 0x0224, {0x862, 0x864, 0x866, 0x868, 0x86A, 0x86C, 0x86E, 0x870}, 0x7D87);
    for (int i = 0; i < 8; ++i) q.set(uint16_t(0x872 + 2 * i), box2[i]);
    q.call(0x1794, 0x0224, {0x872, 0x874, 0x876, 0x878, 0x87A, 0x87C, 0x87E, 0x880}, 0x7DDC);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x68}, 0x7DF4);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x7A}, 0x7E0C);
    q.call(RT, 0x2384, {0x1200, 0x8C}, 0x7E19);          // F$ = "balloon1"
    q.call(0x1664, 0x004B, {0x68, 0x8C, 0x96, 0x94}, 0x7E2E);
    q.set(0x882, 0); q.set(0x884, 0);
    q.call(0x17ED, 0x0059, {0x882, 0x884, 0x146}, 0x7E4B);
    q.set(0x886, 0); q.set(0x888, 4);
    q.call(0x17ED, 0x0059, {0x886, 0x888, 0x746}, 0x7E68);
    q.call(RT, 0x2384, {0x1578, 0x88A}, 0x7E75);         // T$ = "SELECT OPPONENT TO PLAY"
    q.set(0x88E, 0xBE); q.set(0x890, 0x9A); q.set(0x892, 1); q.set(0x894, 0);
    q.call(0x16CD, 0x004B, {0x68, 0x88A, 0x88E, 0x890, 0x746, 0x96, 0x892, 0x894, 0x134}, 0x7EB6);
    q.call(RT, 0x27AC, {0x88A}, 0x7EBF);
    // "   <" + E$ + "C" + E$ + ">omputer    <" + E$ + "H" + E$ + ">uman"
    q.call(RT, 0x23BD, {0x1594, 0x12C4}, 0x7ECC);
    const uint16_t parts[7] = {0x159C, 0x12C4, 0x15A2, 0x12C4, 0x15B4, 0x12C4, 0x15BA};
    const uint16_t rets[7] = {0x7ED6, 0x7EE0, 0x7EEA, 0x7EF4, 0x7EFE, 0x7F08, 0x7F12};
    for (int i = 0; i < 7; ++i) { q.push_ax(); q.push_imm(parts[i]); q.callf(RT, 0x23BD, rets[i]); }
    q.push_ax(); q.push_imm(0x896); q.callf(RT, 0x2384, 0x7F1C);   // U$ = ...
    q.set(0x89A, 0xBE); q.set(0x89C, 0xAE); q.set(0x89E, 1); q.set(0x8A0, 0);
    q.call(0x16CD, 0x004B, {0x68, 0x896, 0x89A, 0x89C, 0x746, 0x96, 0x89E, 0x8A0, 0x134}, 0x7F5D);
    q.call(RT, 0x27AC, {0x896}, 0x7F66);
    if (q.eq(0x42A, 0, 0x7F6D, 0x7F70)) {                 // IF SOUNDOFF% = 0 THEN SOUND 300, D!
        q.push_imm(0x12C); q.push_mem(0x14AE); q.push_mem(0x14AC);
        q.callf(RT, 0x32F8, 0x7F81);
    } else {
        q.at(0x7F81);
    }
    q.callf(0x1932, 0x0054, 0x7F86);
    q.call(RT, 0x2384, {0x15C4, 0x602}, 0x7F93);          // K$ = "CcHh"
    q.call(RT, 0x2384, {0x13CC, 0x606}, 0x7FA0);          // R$ = "X"
    q.call(0x1932, 0x005A, {0x602, 0x606}, 0x7FAD);      // wait for a key in K$ -> R$
    q.call(0x1946, 0x003E, {0x606}, 0x7FB6);             // R$ = UCASE$(R$) [?]
    q.call(RT, 0x23FA, {0x606, 0x159C}, 0x7FC3);         // R$ = "C" ?
    if (q.je(0x7FC5, 0x7FC8)) { q.set(0x3FC, 1); q.jmp(); }
    else { q.at(0x7FD1); q.set(0x3FC, 0); }
    q.at(0x7FD7);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x7D32, false, &select_opponent, "select_opponent"};

}  // namespace
