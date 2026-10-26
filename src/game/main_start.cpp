// Main program, region 0824:0030-01C4 (start): the data files must exist.
//
//   CALL 19AE:0884                                       ' runtime set-up [?]
//   F$ = "BALLOONC.OV0": CALL 195B:003A(F$, FOUND%)       ' FOUND%: DGROUP 3A; file exists [?]
//   IF FOUND% = 0 THEN GOTO NeedFiles                    ' 0824:AD0D: "BALLOON CHALLENGE NEED FILES" screen
//   I% = I%                                              ' (as compiled)
//   ... the same for BALLOONC.OV1, OV2, OV3, BALLOON1.JFT, BALLOON2.JFT, BALLOON3.JFT
//   (continues at 0824:01C4)
#include "common.h"

namespace {

using namespace bc;

void main_start(Cpu& c) {
    Qb q{c};
    q.callf(RT, 0x0884, 0x0035);
    struct File { uint16_t name, var; };
    static const File files[7] = {{0x1190, 0x36}, {0x11A0, 0x3E}, {0x11B0, 0x42}, {0x11C0, 0x46},
                                  {0x11D0, 0x4A}, {0x11E0, 0x4E}, {0x11F0, 0x52}};
    uint16_t b = 0x0035;                                  // each check is 0x39 bytes of code
    for (const File& f : files) {
        q.call(RT, 0x2384, {f.name, f.var}, uint16_t(b + 0x0D));
        q.call(0x195B, 0x003A, {f.var, 0x3A}, uint16_t(b + 0x1A));
        q.call(RT, 0x27AC, {f.var}, uint16_t(b + 0x23));
        c.ax = q.get(0x3A); c.ax = and16(c, c.ax, c.ax); c.icount += 3;   // IF FOUND% = 0
        if (c.flags & F_ZF) {
            q.at(uint16_t(b + 0x2A)); c.icount += 1;                       // jmp
            q.at(uint16_t(b + 0x36)); c.icount += 1;                       // THEN GOTO NeedFiles
            c.ip = 0xAD0D;
            return;
        }
        q.at(uint16_t(b + 0x2D));
        c.ax = q.get(0x3C); wr16(c, c.ds, 0x3C, c.ax); c.icount += 3;      // I% = I%, jmp
        b = uint16_t(b + 0x39);
        if (b != 0x01C4) q.at(b);
    }
    c.ip = 0x01C4;                                        // leaves the region
}

rc::NativeRegion reg{0x0824, 0x0030, 0x0030, 0x01C4, &main_start, "main_start"};

}  // namespace
