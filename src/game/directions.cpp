// GOSUB at 0824:CCB9: the directions (help) pages. Each page: a box (1794:0160), the string array D$()
// (descriptor F10, REDIM'd by runtime 1646 [?]) filled with the page's lines (literals and E$ + ... + E$
// concatenations, E$ = DGROUP 12C4 [?]), a heading printed at the top, then
//   FOR I = 1 TO n: Y = Y + 15: T$ = D$(I): print T$ at 28,Y: D$(I) = T$: NEXT   ' Y: DGROUP F22
// and "<Press any key>"-style footer, keyboard flush and K$ = INPUT$(1) (a lockstep stop).
// Four pages; shareware copies then show the order screen (module 15D6:0033 [?]).
//
// The statement sequence was produced by recomp/recompiler/qblift.py from the compiled code and
// completed by hand (print loops, the final IF); checked with --verify-native.
#include "common.h"

namespace {

using namespace bc;

constexpr uint16_t kReg = 0x10A, kY = 0xF22, kLines = 0xF10;

// FOR I = 1 TO n: Y = Y + 15: T$ = D$(I): print T$ at 28,Y (colour 7AC): D$(I) = T$: NEXT
// a: blocks {init, body, print, store back, NEXT, test, exit}.
void print_lines(Qb& q, uint16_t n, uint16_t t, uint16_t xy, std::initializer_list<uint16_t> blocks) {
    Cpu& c = q.c;
    const uint16_t* a = blocks.begin();
    c.ax = 1; c.icount += 2;                                      // mov ax,1; jmp test
    for (;;) {
        q.at(a[5]);
        wr16(c, c.ds, 0x3C, c.ax); (void)sub16(c, c.ax, n); c.icount += 3;
        const bool le = (c.flags & F_ZF) || (((c.flags & F_SF) != 0) != ((c.flags & F_OF) != 0));
        if (!le) { q.at(a[6]); break; }
        q.at(a[1]);
        wr16(c, c.ds, kY, add16(c, q.get(kY), 0x0F)); c.icount += 1;             // Y = Y + 15
        c.ax = SHL16(c, c.ax, 1); c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.si = kLines; c.icount += 4;
        c.bx = add16(c, c.bx, q.get(kLines + 0x0A)); push16(c, c.bx); c.icount += 2;   // D$(I)
        c.bx = c.ax; c.icount += 1;
        q.push_imm(t);
        wr16(c, c.ss, uint16_t(c.bp - 0x0C), c.bx); c.icount += 1;
        q.callf(RT, 0x2384, a[2]);                                                // T$ = D$(I)
        q.set(xy, 0x1C); q.set(uint16_t(xy + 2), 1); q.set(uint16_t(xy + 4), 0);
        q.call(0x16CD, 0x004B, {0x68, t, xy, kY, 0x7AC, 0x96, uint16_t(xy + 2), uint16_t(xy + 4), 0x134}, a[3]);
        q.push_imm(t); c.si = kLines; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0C)); c.icount += 2;
        c.bx = add16(c, c.bx, q.get(kLines + 0x0A)); push16(c, c.bx); c.icount += 2;
        q.callf(RT, 0x2384, a[4]);                                                // D$(I) = T$
        c.ax = q.get(0x3C); c.ax = inc16(c, c.ax); c.icount += 2;                 // NEXT I
    }
}

void directions(Cpu& c) {
    Qb q{c};
    // CCB9
    q.push_imm(0);
    q.push_imm(0x1);
    q.callf(0x19AE, 0x198C, 0xCCC5);
    // CCC5
    q.set(0x1066, 0x2);
    q.set(0x1068, 0x2);
    q.set(0x106A, 0x27D);
    q.set(0x106C, 0x15B);
    q.set(0x106E, 0x4);
    q.set(0x1070, 0xF);
    q.set(0x1072, 0x2);
    q.set(0x1074, 0x2);
    q.push_imm(0x1066);
    q.push_imm(0x1068);
    q.push_imm(0x106A);
    q.push_imm(0x106C);
    q.push_imm(0x106E);
    q.push_imm(0x1070);
    q.push_imm(0x1072);
    q.push_imm(0x1074);
    q.callf(0x1794, 0x0160, 0xCD1A);
    // CD1A
    q.push_imm(0);
    q.push_imm(0x1);
    q.push_imm(0x2);
    q.push_imm(0x101);
    q.push_imm(0x68);
    q.callf(0x19AE, 0x1646, 0xCD32);
    // CD32
    q.push_imm(0);
    q.push_imm(0x1);
    q.push_imm(0x2);
    q.push_imm(0x101);
    q.push_imm(0x7A);
    q.callf(0x19AE, 0x1646, 0xCD4A);
    // CD4A
    q.push_imm(0x1200);
    q.push_imm(0x8C);
    q.callf(0x19AE, 0x2384, 0xCD57);
    // CD57
    q.push_imm(0x68);
    q.push_imm(0x8C);
    q.push_imm(0x96);
    q.push_imm(0x94);
    q.callf(0x1664, 0x004B, 0xCD6C);
    // CD6C
    q.set(0x1076, 0x1);
    q.set(0x1078, 0x4);
    q.push_imm(0x1076);
    q.push_imm(0x1078);
    q.push_imm(0xF02);
    q.callf(0x17ED, 0x0059, 0xCD89);
    // CD89
    q.push_imm(0x2236);
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCD96);
    // CD96
    q.push_ax();
    q.push_imm(0x2174);
    q.callf(0x19AE, 0x23BD, 0xCDA0);
    // CDA0
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCDAA);
    // CDAA
    q.push_ax();
    q.push_imm(0x270E);
    q.callf(0x19AE, 0x23BD, 0xCDB4);
    // CDB4
    q.push_ax();
    q.push_imm(0x107A);
    q.callf(0x19AE, 0x2384, 0xCDBE);
    // CDBE
    q.set(0x107E, 0xAA);
    q.set(0x1080, 0x142);
    q.set(0x1082, 0x1);
    q.set(0x1084, 0x0);
    q.push_imm(0x68);
    q.push_imm(0x107A);
    q.push_imm(0x107E);
    q.push_imm(0x1080);
    q.push_imm(0xF02);
    q.push_imm(0x96);
    q.push_imm(0x1082);
    q.push_imm(0x1084);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xCDFF);
    // CDFF
    q.push_imm(0x107A);
    q.callf(0x19AE, 0x27AC, 0xCE08);
    // CE08
    q.push_imm(0);
    q.push_imm(0x14);
    q.push_imm(0x4);
    q.push_imm(0x8001);
    q.push_imm(0xF10);
    q.callf(0x19AE, 0x1646, 0xCE20);
    // CE20
    q.set(0xF22, 0x3C);
    q.set(0x1086, 0x4);
    q.set(0x1088, 0x4);
    q.push_imm(0x1086);
    q.push_imm(0x1088);
    q.push_imm(0x7AC);
    q.callf(0x17ED, 0x0059, 0xCE43);
    // CE43
    q.set(0x108A, 0x0);
    q.set(0x108C, 0x0);
    q.push_imm(0x108A);
    q.push_imm(0x108C);
    q.push_imm(0x8FC);
    q.callf(0x17ED, 0x0059, 0xCE60);
    // CE60
    q.push_imm(0x271E);
    q.push_imm(0x108E);
    q.callf(0x19AE, 0x2384, 0xCE6D);
    // CE6D
    q.set(0x1092, 0x80);
    q.set(0x1094, 0xA);
    q.set(0x1096, 0x1);
    q.set(0x1098, 0x1);
    q.push_imm(0x68);
    q.push_imm(0x108E);
    q.push_imm(0x1092);
    q.push_imm(0x1094);
    q.push_imm(0xF02);
    q.push_imm(0x96);
    q.push_imm(0x1096);
    q.push_imm(0x1098);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xCEAE);
    // CEAE
    q.push_imm(0x108E);
    q.callf(0x19AE, 0x27AC, 0xCEB7);
    // CEB7
    q.push_imm(0);
    q.push_imm(0x14);
    q.push_imm(0x4);
    q.push_imm(0x8001);
    q.push_imm(0xF10);
    q.callf(0x19AE, 0x1646, 0xCECF);
    // CECF
    q.push_imm(0x274C);
    q.push_elem_addr(0xF10, 0x4, true);
    q.callf(0x19AE, 0x2384, 0xCEE2);
    // CEE2
    q.push_imm(0x2796);
    q.push_elem_addr(c.si, 0x8, false);
    q.callf(0x19AE, 0x2384, 0xCEF2);
    // CEF2
    q.push_imm(0x27DE);
    q.push_elem_addr(c.si, 0xC, false);
    q.callf(0x19AE, 0x2384, 0xCF02);
    // CF02
    q.push_imm(0x2824);
    q.push_elem_addr(c.si, 0x10, false);
    q.callf(0x19AE, 0x2384, 0xCF12);
    // CF12
    q.push_imm(0x12C4);
    q.push_imm(0x284E);
    q.callf(0x19AE, 0x23BD, 0xCF1F);
    // CF1F
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCF29);
    // CF29
    q.push_ax();
    q.push_elem_addr(c.si, 0x18, false);
    q.callf(0x19AE, 0x2384, 0xCF36);
    // CF36
    q.push_imm(0x2880);
    q.push_elem_addr(c.si, 0x20, false);
    q.callf(0x19AE, 0x2384, 0xCF46);
    // CF46
    q.push_imm(0x28C8);
    q.push_elem_addr(c.si, 0x24, false);
    q.callf(0x19AE, 0x2384, 0xCF56);
    // CF56
    q.push_imm(0x2910);
    q.push_elem_addr(c.si, 0x28, false);
    q.callf(0x19AE, 0x2384, 0xCF66);
    // CF66
    q.push_imm(0x2956);
    q.push_elem_addr(c.si, 0x2C, false);
    q.callf(0x19AE, 0x2384, 0xCF76);
    // CF76
    q.push_imm(0x12C4);
    q.push_imm(0x299C);
    q.callf(0x19AE, 0x23BD, 0xCF83);
    // CF83
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCF8D);
    // CF8D
    q.push_ax();
    q.push_imm(0x29AA);
    q.callf(0x19AE, 0x23BD, 0xCF97);
    // CF97
    q.push_ax();
    q.push_elem_addr(c.si, 0x30, false);
    q.callf(0x19AE, 0x2384, 0xCFA4);
    // CFA4
    q.push_imm(0x29E0);
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCFB1);
    // CFB1
    q.push_ax();
    q.push_imm(0x2A1E);
    q.callf(0x19AE, 0x23BD, 0xCFBB);
    // CFBB
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xCFC5);
    // CFC5
    q.push_ax();
    q.push_elem_addr(c.si, 0x34, false);
    q.callf(0x19AE, 0x2384, 0xCFD2);
    // CFD2
    q.push_imm(0x2A2C);
    q.push_elem_addr(c.si, 0x38, false);
    q.callf(0x19AE, 0x2384, 0xCFE2);
    // CFE2
    q.set(0xF22, 0x38);
    q.set(0x109A, 0x0);
    q.set(0x109C, 0x4);
    q.push_imm(0x109A);
    q.push_imm(0x109C);
    q.push_imm(0x7AC);
    q.callf(0x17ED, 0x0059, 0xD005);
    // D005
    q.push_imm(0x12C4);
    q.push_imm(0x2A34);
    q.callf(0x19AE, 0x23BD, 0xD012);
    // D012
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD01C);
    // D01C
    q.push_ax();
    q.push_imm(0x109E);
    q.callf(0x19AE, 0x2384, 0xD026);
    // D026
    q.set(0x10A2, 0xF8);
    q.set(0x10A4, 0x28);
    q.set(0x10A6, 0x1);
    q.set(0x10A8, 0x1);
    q.push_imm(0x68);
    q.push_imm(0x109E);
    q.push_imm(0x10A2);
    q.push_imm(0x10A4);
    q.push_imm(0x7AC);
    q.push_imm(0x96);
    q.push_imm(0x10A6);
    q.push_imm(0x10A8);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD067);
    // D067
    q.push_imm(0x109E);
    q.callf(0x19AE, 0x27AC, 0xD070);
    // FOR I = 1 TO 14: Y = Y + 15: print D$(I) at 28,Y: NEXT
    print_lines(q, 0xE, 0x10AA, 0x10AE, {0xD070, 0xD076, 0xD096, 0xD0D1, 0xD0E4, 0xD0E8, 0xD0F0});
    // D0F0
    q.callf(0x1932, 0x0054, 0xD0F5);
    // D0F5
    q.push_imm(0x1);
    q.push_imm(0x7FFF);
    q.callf(0x19AE, 0x2B4A, 0xD102);
    // D102
    q.push_ax();
    q.push_imm(0x818);
    q.callf(0x19AE, 0x2384, 0xD10C);
    // D10C
    q.set(0x10B4, 0x2);
    q.set(0x10B6, 0x2);
    q.set(0x10B8, 0x27D);
    q.set(0x10BA, 0x15B);
    q.set(0x10BC, 0x4);
    q.set(0x10BE, 0xF);
    q.set(0x10C0, 0x2);
    q.set(0x10C2, 0x2);
    q.push_imm(0x10B4);
    q.push_imm(0x10B6);
    q.push_imm(0x10B8);
    q.push_imm(0x10BA);
    q.push_imm(0x10BC);
    q.push_imm(0x10BE);
    q.push_imm(0x10C0);
    q.push_imm(0x10C2);
    q.callf(0x1794, 0x0160, 0xD161);
    // D161
    q.push_imm(0);
    q.push_imm(0x14);
    q.push_imm(0x4);
    q.push_imm(0x8001);
    q.push_imm(0xF10);
    q.callf(0x19AE, 0x1646, 0xD179);
    // D179
    q.set(0xF22, 0x32);
    q.push_imm(0x12C4);
    q.push_imm(0x2A42);
    q.callf(0x19AE, 0x23BD, 0xD18C);
    // D18C
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD196);
    // D196
    q.push_ax();
    q.push_imm(0x10C4);
    q.callf(0x19AE, 0x2384, 0xD1A0);
    // D1A0
    q.set(0x10C8, 0xF0);
    q.set(0x10CA, 0x1A);
    q.set(0x10CC, 0x1);
    q.set(0x10CE, 0x1);
    q.push_imm(0x68);
    q.push_imm(0x10C4);
    q.push_imm(0x10C8);
    q.push_imm(0x10CA);
    q.push_imm(0x7AC);
    q.push_imm(0x96);
    q.push_imm(0x10CC);
    q.push_imm(0x10CE);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD1E1);
    // D1E1
    q.push_imm(0x10C4);
    q.callf(0x19AE, 0x27AC, 0xD1EA);
    // D1EA
    q.push_imm(0x2A52);
    q.push_elem_addr(0xF10, 0x4, true);
    q.callf(0x19AE, 0x2384, 0xD1FD);
    // D1FD
    q.push_imm(0x2A98);
    q.push_elem_addr(c.si, 0x8, false);
    q.callf(0x19AE, 0x2384, 0xD20D);
    // D20D
    q.push_imm(0x2ADE);
    q.push_elem_addr(c.si, 0xC, false);
    q.callf(0x19AE, 0x2384, 0xD21D);
    // D21D
    q.push_imm(0x2B24);
    q.push_elem_addr(c.si, 0x10, false);
    q.callf(0x19AE, 0x2384, 0xD22D);
    // D22D
    q.push_imm(0x2B58);
    q.push_elem_addr(c.si, 0x18, false);
    q.callf(0x19AE, 0x2384, 0xD23D);
    // D23D
    q.push_imm(0x37);
    q.push_imm(0xA0);
    q.callf(0x19AE, 0x2AAB, 0xD24A);
    // D24A
    q.push_mem(0x27C);
    q.push_mem(0x27A);
    q.push_imm(0x27A);
    q.push_imm(0x3);
    q.callf(0x19AE, 0x15A4, 0xD25F);
    // D25F
    q.push_imm(0xA5);
    q.push_imm(0xA0);
    q.callf(0x19AE, 0x2AAB, 0xD26C);
    // D26C
    q.push_mem(0x1FE);
    q.push_mem(0x1FC);
    q.push_imm(0x1FC);
    q.push_imm(0x3);
    q.callf(0x19AE, 0x15A4, 0xD281);
    // D281
    q.push_imm(0x13B);
    q.push_imm(0xA0);
    q.callf(0x19AE, 0x2AAB, 0xD28E);
    // D28E
    q.push_mem(0x1C8);
    q.push_mem(0x1C6);
    q.push_imm(0x1C6);
    q.push_imm(0x3);
    q.callf(0x19AE, 0x15A4, 0xD2A3);
    // D2A3
    q.push_imm(0x1C7);
    q.push_imm(0xA0);
    q.callf(0x19AE, 0x2AAB, 0xD2B0);
    // D2B0
    q.push_mem(0x1DA);
    q.push_mem(0x1D8);
    q.push_imm(0x1D8);
    q.push_imm(0x3);
    q.callf(0x19AE, 0x15A4, 0xD2C5);
    // D2C5
    q.push_imm(0x228);
    q.push_imm(0xA0);
    q.callf(0x19AE, 0x2AAB, 0xD2D2);
    // D2D2
    q.push_mem(0x2E8);
    q.push_mem(0x2E6);
    q.push_imm(0x2E6);
    q.push_imm(0x3);
    q.callf(0x19AE, 0x15A4, 0xD2E7);
    // D2E7
    q.push_imm(0x12C4);
    q.push_imm(0x2B9E);
    q.callf(0x19AE, 0x23BD, 0xD2F4);
    // D2F4
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD2FE);
    // D2FE
    q.push_ax();
    q.push_imm(0x2BAC);
    q.callf(0x19AE, 0x23BD, 0xD308);
    // D308
    q.push_ax();
    q.push_elem_addr(c.si, 0x24, false);
    q.callf(0x19AE, 0x2384, 0xD315);
    // D315
    q.push_imm(0x2BEA);
    q.push_elem_addr(c.si, 0x28, false);
    q.callf(0x19AE, 0x2384, 0xD325);
    // D325
    q.push_imm(0x12C4);
    q.push_imm(0x2C28);
    q.callf(0x19AE, 0x23BD, 0xD332);
    // D332
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD33C);
    // D33C
    q.push_ax();
    q.push_imm(0x2C38);
    q.callf(0x19AE, 0x23BD, 0xD346);
    // D346
    q.push_ax();
    q.push_elem_addr(c.si, 0x30, false);
    q.callf(0x19AE, 0x2384, 0xD353);
    // D353
    q.push_imm(0x12C4);
    q.push_imm(0x2C66);
    q.callf(0x19AE, 0x23BD, 0xD360);
    // D360
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD36A);
    // D36A
    q.push_ax();
    q.push_imm(0x2C7C);
    q.callf(0x19AE, 0x23BD, 0xD374);
    // D374
    q.push_ax();
    q.push_elem_addr(c.si, 0x38, false);
    q.callf(0x19AE, 0x2384, 0xD381);
    // D381
    q.push_imm(0x2CB2);
    q.push_elem_addr(c.si, 0x3C, false);
    q.callf(0x19AE, 0x2384, 0xD391);
    // D391
    q.push_imm(0x2CF8);
    q.push_elem_addr(c.si, 0x40, false);
    q.callf(0x19AE, 0x2384, 0xD3A1);
    // FOR I = 1 TO 16: Y = Y + 15: print D$(I) at 28,Y: NEXT
    print_lines(q, 0x10, 0x10D0, 0x10D4, {0xD3A1, 0xD3A8, 0xD3C8, 0xD403, 0xD416, 0xD41A, 0xD422});
    // D422
    q.push_imm(0x2236);
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD42F);
    // D42F
    q.push_ax();
    q.push_imm(0x2174);
    q.callf(0x19AE, 0x23BD, 0xD439);
    // D439
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD443);
    // D443
    q.push_ax();
    q.push_imm(0x270E);
    q.callf(0x19AE, 0x23BD, 0xD44D);
    // D44D
    q.push_ax();
    q.push_imm(0x10DA);
    q.callf(0x19AE, 0x2384, 0xD457);
    // D457
    q.set(0x10DE, 0xAA);
    q.set(0x10E0, 0x142);
    q.set(0x10E2, 0x1);
    q.set(0x10E4, 0x0);
    q.push_imm(0x68);
    q.push_imm(0x10DA);
    q.push_imm(0x10DE);
    q.push_imm(0x10E0);
    q.push_imm(0xF02);
    q.push_imm(0x96);
    q.push_imm(0x10E2);
    q.push_imm(0x10E4);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD498);
    // D498
    q.push_imm(0x10DA);
    q.callf(0x19AE, 0x27AC, 0xD4A1);
    // D4A1
    q.callf(0x1932, 0x0054, 0xD4A6);
    // D4A6
    q.push_imm(0x1);
    q.push_imm(0x7FFF);
    q.callf(0x19AE, 0x2B4A, 0xD4B3);
    // D4B3
    q.push_ax();
    q.push_imm(0x818);
    q.callf(0x19AE, 0x2384, 0xD4BD);
    // D4BD
    q.set(0x10E6, 0x2);
    q.set(0x10E8, 0x2);
    q.set(0x10EA, 0x27D);
    q.set(0x10EC, 0x15B);
    q.set(0x10EE, 0x4);
    q.set(0x10F0, 0xF);
    q.set(0x10F2, 0x2);
    q.set(0x10F4, 0x2);
    q.push_imm(0x10E6);
    q.push_imm(0x10E8);
    q.push_imm(0x10EA);
    q.push_imm(0x10EC);
    q.push_imm(0x10EE);
    q.push_imm(0x10F0);
    q.push_imm(0x10F2);
    q.push_imm(0x10F4);
    q.callf(0x1794, 0x0160, 0xD512);
    // D512
    q.push_imm(0);
    q.push_imm(0x14);
    q.push_imm(0x4);
    q.push_imm(0x8001);
    q.push_imm(0xF10);
    q.callf(0x19AE, 0x1646, 0xD52A);
    // D52A
    q.set(0xF22, 0x2D);
    q.push_imm(0x12C4);
    q.push_imm(0x2A42);
    q.callf(0x19AE, 0x23BD, 0xD53D);
    // D53D
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD547);
    // D547
    q.push_ax();
    q.push_imm(0x2D1A);
    q.callf(0x19AE, 0x23BD, 0xD551);
    // D551
    q.push_ax();
    q.push_imm(0x10F6);
    q.callf(0x19AE, 0x2384, 0xD55B);
    // D55B
    q.set(0x10FA, 0xC5);
    q.set(0x10FC, 0x17);
    q.set(0x10FE, 0x1);
    q.set(0x1100, 0x1);
    q.push_imm(0x68);
    q.push_imm(0x10F6);
    q.push_imm(0x10FA);
    q.push_imm(0x10FC);
    q.push_imm(0x7AC);
    q.push_imm(0x96);
    q.push_imm(0x10FE);
    q.push_imm(0x1100);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD59C);
    // D59C
    q.push_imm(0x10F6);
    q.callf(0x19AE, 0x27AC, 0xD5A5);
    // D5A5
    q.push_imm(0x12C4);
    q.push_imm(0x2D2C);
    q.callf(0x19AE, 0x23BD, 0xD5B2);
    // D5B2
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD5BC);
    // D5BC
    q.push_ax();
    q.push_imm(0x2D3A);
    q.callf(0x19AE, 0x23BD, 0xD5C6);
    // D5C6
    q.push_ax();
    q.push_elem_addr(0xF10, 0x4, true);
    q.callf(0x19AE, 0x2384, 0xD5D6);
    // D5D6
    q.push_imm(0x2D76);
    q.push_elem_addr(c.si, 0x8, false);
    q.callf(0x19AE, 0x2384, 0xD5E6);
    // D5E6
    q.push_imm(0x12C4);
    q.push_imm(0x2DB6);
    q.callf(0x19AE, 0x23BD, 0xD5F3);
    // D5F3
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD5FD);
    // D5FD
    q.push_ax();
    q.push_imm(0x2DC8);
    q.callf(0x19AE, 0x23BD, 0xD607);
    // D607
    q.push_ax();
    q.push_elem_addr(c.si, 0x10, false);
    q.callf(0x19AE, 0x2384, 0xD614);
    // D614
    q.push_imm(0x2E04);
    q.push_elem_addr(c.si, 0x14, false);
    q.callf(0x19AE, 0x2384, 0xD624);
    // D624
    q.push_imm(0x2E4A);
    q.push_elem_addr(c.si, 0x18, false);
    q.callf(0x19AE, 0x2384, 0xD634);
    // D634
    q.push_imm(0x12C4);
    q.push_imm(0x2E78);
    q.callf(0x19AE, 0x23BD, 0xD641);
    // D641
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD64B);
    // D64B
    q.push_ax();
    q.push_elem_addr(c.si, 0x20, false);
    q.callf(0x19AE, 0x2384, 0xD658);
    // D658
    q.push_imm(0x2EA8);
    q.push_elem_addr(c.si, 0x28, false);
    q.callf(0x19AE, 0x2384, 0xD668);
    // D668
    q.push_imm(0x2EF2);
    q.push_elem_addr(c.si, 0x2C, false);
    q.callf(0x19AE, 0x2384, 0xD678);
    // D678
    q.push_imm(0x2F3C);
    q.push_elem_addr(c.si, 0x30, false);
    q.callf(0x19AE, 0x2384, 0xD688);
    // D688
    q.push_imm(0x2F84);
    q.push_elem_addr(c.si, 0x34, false);
    q.callf(0x19AE, 0x2384, 0xD698);
    // D698
    q.push_imm(0x2FCC);
    q.push_elem_addr(c.si, 0x38, false);
    q.callf(0x19AE, 0x2384, 0xD6A8);
    // D6A8
    q.push_imm(0x3014);
    q.push_elem_addr(c.si, 0x40, false);
    q.callf(0x19AE, 0x2384, 0xD6B8);
    // D6B8
    q.push_imm(0x305A);
    q.push_elem_addr(c.si, 0x44, false);
    q.callf(0x19AE, 0x2384, 0xD6C8);
    // FOR I = 1 TO 17: Y = Y + 15: print D$(I) at 28,Y: NEXT
    print_lines(q, 0x11, 0x1102, 0x1106, {0xD6C8, 0xD6CE, 0xD6EE, 0xD729, 0xD73C, 0xD740, 0xD748});
    // D748
    q.push_imm(0x2236);
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD755);
    // D755
    q.push_ax();
    q.push_imm(0x2174);
    q.callf(0x19AE, 0x23BD, 0xD75F);
    // D75F
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD769);
    // D769
    q.push_ax();
    q.push_imm(0x270E);
    q.callf(0x19AE, 0x23BD, 0xD773);
    // D773
    q.push_ax();
    q.push_imm(0x110C);
    q.callf(0x19AE, 0x2384, 0xD77D);
    // D77D
    q.set(0x1110, 0xAA);
    q.set(0x1112, 0x142);
    q.set(0x1114, 0x1);
    q.set(0x1116, 0x0);
    q.push_imm(0x68);
    q.push_imm(0x110C);
    q.push_imm(0x1110);
    q.push_imm(0x1112);
    q.push_imm(0xF02);
    q.push_imm(0x96);
    q.push_imm(0x1114);
    q.push_imm(0x1116);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD7BE);
    // D7BE
    q.push_imm(0x110C);
    q.callf(0x19AE, 0x27AC, 0xD7C7);
    // D7C7
    q.callf(0x1932, 0x0054, 0xD7CC);
    // D7CC
    q.push_imm(0x1);
    q.push_imm(0x7FFF);
    q.callf(0x19AE, 0x2B4A, 0xD7D9);
    // D7D9
    q.push_ax();
    q.push_imm(0x818);
    q.callf(0x19AE, 0x2384, 0xD7E3);
    // D7E3
    q.push_imm(0);
    q.push_imm(0x14);
    q.push_imm(0x4);
    q.push_imm(0x8001);
    q.push_imm(0xF10);
    q.callf(0x19AE, 0x1646, 0xD7FB);
    // D7FB
    q.set(0xF22, 0x2D);
    q.set(0x1118, 0x2);
    q.set(0x111A, 0x2);
    q.set(0x111C, 0x27D);
    q.set(0x111E, 0x15B);
    q.set(0x1120, 0x4);
    q.set(0x1122, 0xF);
    q.set(0x1124, 0x2);
    q.set(0x1126, 0x2);
    q.push_imm(0x1118);
    q.push_imm(0x111A);
    q.push_imm(0x111C);
    q.push_imm(0x111E);
    q.push_imm(0x1120);
    q.push_imm(0x1122);
    q.push_imm(0x1124);
    q.push_imm(0x1126);
    q.callf(0x1794, 0x0160, 0xD856);
    // D856
    q.push_imm(0x12C4);
    q.push_imm(0x309E);
    q.callf(0x19AE, 0x23BD, 0xD863);
    // D863
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xD86D);
    // D86D
    q.push_ax();
    q.push_imm(0x1128);
    q.callf(0x19AE, 0x2384, 0xD877);
    // D877
    q.set(0x112C, 0xE6);
    q.set(0x112E, 0x17);
    q.set(0x1130, 0x1);
    q.set(0x1132, 0x1);
    q.push_imm(0x68);
    q.push_imm(0x1128);
    q.push_imm(0x112C);
    q.push_imm(0x112E);
    q.push_imm(0x7AC);
    q.push_imm(0x96);
    q.push_imm(0x1130);
    q.push_imm(0x1132);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xD8B8);
    // D8B8
    q.push_imm(0x1128);
    q.callf(0x19AE, 0x27AC, 0xD8C1);
    // D8C1
    q.push_imm(0x30B0);
    q.push_elem_addr(0xF10, 0x4, true);
    q.callf(0x19AE, 0x2384, 0xD8D4);
    // D8D4
    q.push_imm(0x30F4);
    q.push_elem_addr(c.si, 0x8, false);
    q.callf(0x19AE, 0x2384, 0xD8E4);
    // D8E4
    q.push_imm(0x3138);
    q.push_elem_addr(c.si, 0xC, false);
    q.callf(0x19AE, 0x2384, 0xD8F4);
    // D8F4
    q.push_imm(0x317A);
    q.push_elem_addr(c.si, 0x10, false);
    q.callf(0x19AE, 0x2384, 0xD904);
    // D904
    q.push_imm(0x319E);
    q.push_elem_addr(c.si, 0x20, false);
    q.callf(0x19AE, 0x2384, 0xD914);
    // D914
    q.push_imm(0x31E2);
    q.push_elem_addr(c.si, 0x24, false);
    q.callf(0x19AE, 0x2384, 0xD924);
    // D924
    q.push_imm(0x322A);
    q.push_elem_addr(c.si, 0x28, false);
    q.callf(0x19AE, 0x2384, 0xD934);
    // D934
    q.push_imm(0x326E);
    q.push_elem_addr(c.si, 0x2C, false);
    q.callf(0x19AE, 0x2384, 0xD944);
    // D944
    q.push_imm(0x32B2);
    q.push_elem_addr(c.si, 0x30, false);
    q.callf(0x19AE, 0x2384, 0xD954);
    // D954
    q.push_imm(0x32F8);
    q.push_elem_addr(c.si, 0x34, false);
    q.callf(0x19AE, 0x2384, 0xD964);
    // D964
    q.push_imm(0x333C);
    q.push_elem_addr(c.si, 0x38, false);
    q.callf(0x19AE, 0x2384, 0xD974);
    // D974
    q.push_imm(0x3376);
    q.push_elem_addr(c.si, 0x40, false);
    q.callf(0x19AE, 0x2384, 0xD984);
    // FOR I = 1 TO 16: Y = Y + 15: print D$(I) at 28,Y: NEXT
    print_lines(q, 0x10, 0x1134, 0x1138, {0xD984, 0xD98A, 0xD9AA, 0xD9E5, 0xD9F8, 0xD9FC, 0xDA04});
    // DA04
    q.push_imm(0x2236);
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xDA11);
    // DA11
    q.push_ax();
    q.push_imm(0x2174);
    q.callf(0x19AE, 0x23BD, 0xDA1B);
    // DA1B
    q.push_ax();
    q.push_imm(0x12C4);
    q.callf(0x19AE, 0x23BD, 0xDA25);
    // DA25
    q.push_ax();
    q.push_imm(0x2182);
    q.callf(0x19AE, 0x23BD, 0xDA2F);
    // DA2F
    q.push_ax();
    q.push_imm(0x113E);
    q.callf(0x19AE, 0x2384, 0xDA39);
    // DA39
    q.set(0x1142, 0x96);
    q.set(0x1144, 0x142);
    q.set(0x1146, 0x1);
    q.set(0x1148, 0x0);
    q.push_imm(0x68);
    q.push_imm(0x113E);
    q.push_imm(0x1142);
    q.push_imm(0x1144);
    q.push_imm(0xF02);
    q.push_imm(0x96);
    q.push_imm(0x1146);
    q.push_imm(0x1148);
    q.push_imm(0x134);
    q.callf(0x16CD, 0x004B, 0xDA7A);
    // DA7A
    q.push_imm(0x113E);
    q.callf(0x19AE, 0x27AC, 0xDA83);
    // DA83
    q.callf(0x1932, 0x0054, 0xDA88);
    // DA88
    q.push_imm(0x1);
    q.push_imm(0x7FFF);
    q.callf(0x19AE, 0x2B4A, 0xDA95);
    // DA95
    q.push_ax();
    q.push_imm(0x818);
    q.callf(0x19AE, 0x2384, 0xDA9F);
    if (q.eq(kReg, 0, 0xDAA6, 0xDAA9)) {          // IF REG% = 0 THEN (shareware: order screen)
    // DAA9
        q.set(0xC42, 0x1);
        q.push_imm(0xC42);
        q.callf(0x15D6, 0x0033, 0xDAB8);
    // DAB8
        q.set(0xC42, 0x0);
        q.push_imm(0);
        q.push_imm(0x1);
        q.push_imm(0x2);
        q.push_imm(0x101);
        q.push_imm(0x68);
        q.callf(0x19AE, 0x1646, 0xDAD6);
    // DAD6
        q.push_imm(0);
        q.push_imm(0x1);
        q.push_imm(0x2);
        q.push_imm(0x101);
        q.push_imm(0x7A);
        q.callf(0x19AE, 0x1646, 0xDAEE);
    // DAEE
        q.push_imm(0x1200);
        q.push_imm(0x8C);
        q.callf(0x19AE, 0x2384, 0xDAFB);
    // DAFB
        q.push_imm(0x68);
        q.push_imm(0x8C);
        q.push_imm(0x96);
        q.push_imm(0x94);
        q.callf(0x1664, 0x004B, 0xDB10);
    } else {
        q.at(0xDB10);
    }
    // DB10
    q.push_imm(0x1);
    q.push_imm(0);
    q.callf(0x19AE, 0x198C, 0xDB1C);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0xCCB9, false, &directions, "directions"};

}  // namespace
