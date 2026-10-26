// GOSUB at 0824:521B: title line, menu line and the registered/shareware banners.
//
//   CALL 17ED:0059(15, 14, 49A)                                          ' [?]
//   T$ = "B A L L O O N   C H A L L E N G E" + E$ + "    by Soleau Software" + E$   ' E$: DGROUP 12C4 [?]
//   print T$ at 80,0 (colour 49A): release T$
//   CALL 17ED:0059(15, 14, 132)
//   D$ = "<" + E$ + "D" + E$ + ">irections        "                    ' and O$, N$, Q$ likewise
//   M$ = D$ + O$ + N$ + Q$: print M$ at 83,329 (colour 132)              ' menu line (not released)
//   (runtime 1646 on 68, 7A); F$ = "balloon2": FONTSIZE% (DGROUP 94) = -1: font load F$   ' [?]
//   T$ = E$ + " (C) 1993-1995" + E$: print at 564,-4: release
//   IF REG% = 1 THEN print E$ + "Registered" + E$ at 578,10 and E$ + "   Copy" + E$ at 578,18 (right side)
//   IF REG% = 1 THEN the same at 15,10 / 15,18 (left side)
//   IF REG% = 0 THEN print E$ + "Shareware" + E$, E$ + " Version" + E$ at 15,10 / 15,18 and 578,10 / 578,18
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "common.h"

namespace {

using namespace bc;

constexpr uint16_t kE = 0x12C4;   // string variable placed around texts [?]
constexpr uint16_t kReg = 0x10A;

// var = p0 + p1 + ... (runtime 23BD concatenations, 2384 assignment). rets: the return point of each call.
void concat_assign(Qb& q, std::initializer_list<uint16_t> parts, uint16_t var, std::initializer_list<uint16_t> rets) {
    const std::vector<uint16_t> p(parts), r(rets);
    q.call(RT, 0x23BD, {p[0], p[1]}, r[0]);
    for (std::size_t i = 2; i < p.size(); ++i) { q.push_ax(); q.push_imm(p[i]); q.callf(RT, 0x23BD, r[i - 1]); }
    q.push_ax(); q.push_imm(var); q.callf(RT, 0x2384, r[p.size() - 1]);
}

// print var at (x, y) in colour descriptor `colour` (font module 16CD:004B), coordinates in xy..xy+6.
void print(Qb& q, uint16_t var, uint16_t xy, uint16_t x, uint16_t y, uint16_t colour, uint16_t ret) {
    q.set(xy, x); q.set(uint16_t(xy + 2), y); q.set(uint16_t(xy + 4), 1); q.set(uint16_t(xy + 6), 0);
    q.call(0x16CD, 0x004B, {0x68, var, xy, uint16_t(xy + 2), colour, 0x96, uint16_t(xy + 4), uint16_t(xy + 6), 0x134}, ret);
}

void release(Qb& q, uint16_t var, uint16_t ret) { q.call(RT, 0x27AC, {var}, ret); }

// IF REG% = want THEN T$ = E$ + text + E$: print T$ at (x, y): release T$ — blocks: cmp at `at`, then the
// jmp block, then the THEN block (= at + 10), `after` is the next statement.
void banner(Qb& q, uint16_t want, uint16_t text, uint16_t var, uint16_t xy, uint16_t x, uint16_t y,
            uint16_t at, uint16_t after) {
    if (q.eq(kReg, want, uint16_t(at + 7), uint16_t(at + 10))) {
        const uint16_t b = uint16_t(at + 10);   // ARGS E$, text; call (13 bytes) then 2 x 10 bytes, then print
        concat_assign(q, {kE, text, kE}, var, {uint16_t(b + 13), uint16_t(b + 23), uint16_t(b + 33)});
        print(q, var, xy, x, y, 0x132, uint16_t(b + 98));
        release(q, var, after);
    } else {
        q.at(after);
    }
}

void title_screen(Cpu& c) {
    Qb q{c};
    q.set(0x4D4, 15); q.set(0x4D6, 14);
    q.call(0x17ED, 0x0059, {0x4D4, 0x4D6, 0x49A}, 0x5238);
    concat_assign(q, {0x129E, kE, 0x12CA, kE}, 0x4D8, {0x5245, 0x524F, 0x5259, 0x5263});
    print(q, 0x4D8, 0x4DC, 0x50, 0, 0x49A, 0x52A4);
    release(q, 0x4D8, 0x52AD);
    q.set(0x4E4, 15); q.set(0x4E6, 14);
    q.call(0x17ED, 0x0059, {0x4E4, 0x4E6, 0x132}, 0x52CA);
    concat_assign(q, {0x12E4, kE, 0x12EA, kE, 0x12F0}, 0x4E8, {0x52D7, 0x52E1, 0x52EB, 0x52F5, 0x52FF});  // <D>irections
    concat_assign(q, {0x12E4, kE, 0x1306, kE, 0x130C}, 0x4EC, {0x530C, 0x5316, 0x5320, 0x532A, 0x5334});  // <O>ptions
    concat_assign(q, {0x12E4, kE, 0x1320, kE, 0x1326}, 0x4F0, {0x5341, 0x534B, 0x5355, 0x535F, 0x5369});  // <N>ew Game
    concat_assign(q, {0x12E4, kE, 0x133A, kE, 0x1340}, 0x4F4, {0x5376, 0x5380, 0x538A, 0x5394, 0x539E});  // <Q>uit
    concat_assign(q, {0x4E8, 0x4EC, 0x4F8, 0x4F0, 0x4F4}, 0x4FC, {0x53AB, 0x53B5, 0x53BF, 0x53C9, 0x53D3});
    print(q, 0x4FC, 0x500, 0x53, 0x149, 0x132, 0x5414);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x68}, 0x542C);
    q.call(RT, 0x1646, {0, 1, 2, 0x101, 0x7A}, 0x5444);
    q.call(RT, 0x2384, {0x1292, 0x8C}, 0x5451);           // F$ = "balloon2"
    q.set(0x94, 0xFFFF);
    q.call(0x1664, 0x004B, {0x68, 0x8C, 0x96, 0x94}, 0x546C);
    concat_assign(q, {kE, 0x1348, kE}, 0x508, {0x5479, 0x5483, 0x548D});   // " (C) 1993-1995"
    print(q, 0x508, 0x50C, 0x234, 0xFFFC, 0x132, 0x54CE);
    release(q, 0x508, 0x54D7);
    banner(q, 1, 0x135A, 0x514, 0x518, 0x242, 0x0A, 0x54D7, 0x554C);   // "Registered"
    banner(q, 1, 0x1368, 0x520, 0x524, 0x242, 0x12, 0x554C, 0x55C1);   // "   Copy"
    banner(q, 1, 0x135A, 0x52C, 0x530, 0x0F, 0x0A, 0x55C1, 0x5636);
    banner(q, 1, 0x1368, 0x538, 0x53C, 0x0F, 0x12, 0x5636, 0x56AB);
    banner(q, 0, 0x1374, 0x544, 0x548, 0x0F, 0x0A, 0x56AB, 0x5720);    // "Shareware"
    banner(q, 0, 0x1382, 0x550, 0x554, 0x0F, 0x12, 0x5720, 0x5795);    // " Version"
    banner(q, 0, 0x1374, 0x55C, 0x560, 0x242, 0x0A, 0x5795, 0x580A);
    banner(q, 0, 0x1382, 0x568, 0x56C, 0x242, 0x12, 0x580A, 0x587F);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0x521B, false, &title_screen, "title_screen"};

}  // namespace
