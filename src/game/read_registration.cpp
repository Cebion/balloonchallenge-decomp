// GOSUB at 0824:AD7B (startup): reads the registration state from BALLOONC.OV2 into REG% (DGROUP 10A).
//
//   F$ = "BALLOONC.OV2": CALL 195B:003A(F$, FOUND%)        ' FOUND% = 3A: file present [?]
//   IF FOUND% = 0 THEN RETURN
//   I% = I%                                                 ' DGROUP 3C (as compiled)
//   OPEN "BALLOONC.OV2" FOR INPUT AS #3                     ' runtime 0436
//   INPUT #3, A$          ' D32: name line                  ' runtime 2BCC (channel), 22EC, 221E
//   INPUT #3, B$          ' D36: code line
//   K$ = "BALLOON": CALL 1946:0000(B$, K$)                  ' transforms B$ with key K$ [?]
//   INPUT #3, C$          ' D3E
//   CLOSE #3                                                ' runtime 057D (3, 1) [?]
//   IF B$ = <fixed 7-character value built with CHR$> THEN REG% = 1
//   IF REG% = 1 THEN CALL 1946:0000(C$, B$): IF C$ = A$ THEN REG% = 1 ELSE REG% = 0
//   IF REG% = 1 THEN N$ = C$                                ' DGROUP 15E
//   RETURN
//
// The registered test state (lockstep pokes at 0824:AEFE) is announced with q.at().
#include "qb.h"

namespace {

using rc::qb::Qb;

constexpr uint16_t RT = 0x19AE;
constexpr uint16_t kReg = 0x10A, kFound = 0x3A, kA = 0xD32, kB = 0xD36, kK = 0xD3A, kC = 0xD3E, kF = 0xD2E;

// INPUT #3, var: select the channel, read the variable, end the statement.
void input3(Qb& q, uint16_t var, uint16_t ret_chan, uint16_t ret_read, uint16_t ret_end) {
    q.call(RT, 0x2BCC, {3}, ret_chan);
    q.call_ds_var(RT, 0x22EC, var, ret_read);
    q.callf(RT, 0x221E, ret_end);
}

// CHR$(code) pushed onto the expression stack: `mov ax,code; push ax; call 2439; push ax`.
void chr(Qb& q, uint16_t code, uint16_t ret) {
    q.push_imm(code);
    q.callf(RT, 0x2439, ret);
}

void read_registration(rc::Cpu& c) {
    Qb q{c};
    q.call(RT, 0x2384, {0x11B0, kF}, 0xAD88);            // F$ = "BALLOONC.OV2"
    q.call(0x195B, 0x003A, {kF, kFound}, 0xAD95);
    q.call(RT, 0x27AC, {kF}, 0xAD9E);
    c.ax = q.get(kFound); c.ax = rc::and16(c, c.ax, c.ax); c.icount += 3;  // IF FOUND% = 0 THEN RETURN
    if (!(c.flags & rc::F_ZF)) {
        q.at(0xADA8);
        c.ax = q.get(0x3C); rc::wr16(c, c.ds, 0x3C, c.ax); c.icount += 3;  // I% = I%, jmp
        q.at(0xADB2);
    } else {
        q.at(0xADA5);
        c.icount += 1;                                                  // jmp to the RETURN
        q.at(0xADB1);
        q.ret_near();
        return;
    }
    q.call(RT, 0x0436, {0x11B0, 3, 0xFFFF, 1}, 0xADC7);  // OPEN ... FOR INPUT AS #3
    input3(q, kA, 0xADD0, 0xADDD, 0xADE2);
    input3(q, kB, 0xADEB, 0xADF8, 0xADFD);
    q.call(RT, 0x2384, {0x2058, kK}, 0xAE0A);            // K$ = "BALLOON"
    q.call(0x1946, 0x0000, {kB, kK}, 0xAE17);
    q.call(RT, 0x27AC, {kK}, 0xAE20);
    input3(q, kC, 0xAE29, 0xAE36, 0xAE3B);
    q.call(RT, 0x057D, {3, 1}, 0xAE48);                  // CLOSE #3
    // B$ = CHR$(..) + CHR$(..) + ... (7 characters), concatenated with runtime 23BD
    chr(q, 0x38, 0xAE51); q.push_ax();
    chr(q, 0x38, 0xAE5B); q.push_ax(); q.callf(RT, 0x23BD, 0xAE61); q.push_ax();
    chr(q, 0x39, 0xAE6B); q.push_ax(); q.callf(RT, 0x23BD, 0xAE71); q.push_ax();
    chr(q, 0x39, 0xAE7B); q.push_ax(); q.callf(RT, 0x23BD, 0xAE81); q.push_ax();
    chr(q, 0x34, 0xAE8B); q.push_ax(); q.callf(RT, 0x23BD, 0xAE91); q.push_ax();
    chr(q, 0x35, 0xAE9B); q.push_ax(); q.callf(RT, 0x23BD, 0xAEA1); q.push_ax();
    chr(q, 0x32, 0xAEAB); q.push_ax(); q.callf(RT, 0x23BD, 0xAEB1); q.push_ax();
    q.push_imm(kB);
    q.callf(RT, 0x23FA, 0xAEBB);                          // compare with B$
    if (q.je(0xAEBD, 0xAEC0)) q.set(kReg, 1);          // (the SET falls through into the next IF)
    q.at(0xAEC6);
    if (q.eq(kReg, 1, 0xAECD, 0xAED0)) {
        q.call(0x1946, 0x0000, {kC, kB}, 0xAEDD);
        q.call(RT, 0x23FA, {kC, kA}, 0xAEEA);             // C$ = A$ ?
        if (q.je(0xAEEC, 0xAEEF)) { q.set(kReg, 1); q.jmp(); }
        else { q.at(0xAEF8); q.set(kReg, 0); }
    }
    q.at(0xAEFE);
    if (q.eq(kReg, 1, 0xAF05, 0xAF08)) q.call(RT, 0x2384, {kC, 0x15E}, 0xAF15);  // N$ = C$
    else q.at(0xAF15);
    q.ret_near();
}

rc::NativeReg reg{0x0824, 0xAD7B, false, &read_registration, "read_registration"};

}  // namespace
