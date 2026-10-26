// GOSUB at 0824:5A27: prints "Thinking" (literal 138E) at 563,230 with the balloon1 font while the computer
// plans its move. A counter (0, 1, 2) selects which of two calls to module routine 17ED:0059 runs: with
// (0, 0) on the first call, with (14, 14) on the second, which also resets the counter.
// Names marked [?] are not yet established from evidence.
// Decompiled from the recompiled procedure; checked call by call (MachineOptions::Native::Verify).
#include "qb.h"

namespace {

using rc::qb::Qb;

constexpr uint16_t RT = 0x19AE;      // QuickBASIC runtime segment
constexpr uint16_t kBlinkPhase = 0x57C;  // counts calls 0, 1, 2 -> back to 0

void show_thinking(rc::Cpu& c) {
    Qb q{c};
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x68}, 0x5A3F);  // runtime 1646 on descriptor 68 [?]
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x7A}, 0x5A57);  // runtime 1646 on descriptor 7A [?]
    q.call(RT, 0x2384, {0x1200, 0x8C}, 0x5A64);         // F$(8C) = "balloon1" (string assign [?])
    q.call(0x1664, 0x004B, {0x68, 0x8C, 0x96, 0x94}, 0x5A79);  // font module: load F$ ("balloon1") [?]
    q.inc(kBlinkPhase);
    if (q.eq(kBlinkPhase, 1, 0x5A84, 0x5A87)) {
        q.set(0x57E, 0);
        q.set(0x580, 0);
        q.call(0x17ED, 0x0059, {0x57E, 0x580, 0x582}, 0x5AA4);  // 17ED:0059 (0, 0) [?]
    } else {
        q.at(0x5AA4);
    }
    if (q.ge(kBlinkPhase, 2, 0x5AAB, 0x5AAE)) {
        q.set(kBlinkPhase, 0);
        q.set(0x584, 14);
        q.set(0x586, 14);
        q.call(0x17ED, 0x0059, {0x584, 0x586, 0x582}, 0x5AD1);  // 17ED:0059 (14, 14) [?]
    } else {
        q.at(0x5AD1);
    }
    q.call(RT, 0x2384, {0x138E, 0x588}, 0x5ADE);          // T$(588) = "Thinking"
    q.set(0x58C, 0x233);                                  // x = 563
    q.set(0x58E, 0xE6);                                   // y = 230
    q.set(0x590, 1);
    q.set(0x592, 0);
    q.call(0x16CD, 0x004B, {0x68, 0x588, 0x58C, 0x58E, 0x582, 0x96, 0x590, 0x592, 0x134}, 0x5B1F);  // font print T$ at x, y [?]
    q.call(RT, 0x27AC, {0x588}, 0x5B28);                  // runtime 27AC on T$ (string release [?])
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x5A27, false, &show_thinking, "show_thinking"};

}  // namespace
