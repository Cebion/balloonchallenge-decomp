// GOSUB at 0824:5880: two passes over the balloons (count in N, DGROUP 3B8; indices 1..N in the integer
// arrays at descriptors 38A (row) and 39C (column); pixel positions in X() 2F8 and Y() 30A).
//
//   FOR I = 1 TO N                                  ' I is DGROUP 3C
//     R = ROW(I): C = COL(I)                        ' 576, 578
//     K = POINT(X(R) + 6, Y(C) + 11)                ' 3BA; runtime 10EB [?POINT]
//     IF K = 13 THEN PUT (X(R), Y(C)), A1, 3        ' sprite descriptor 1EA; runtime 2AAB, 15A4 [?]
//     ROW(I) = R + 1: IF ROW(I) = 26 THEN ROW(I) = 1
//   NEXT
//   FOR I = 1 TO N
//     (same reads)  IF K = 1 THEN PUT (X(R), Y(C)), A2, 3   ' sprite descriptor 27A
//   NEXT
//
// Statement-by-statement translation of the compiled code (registers, flags, stack-frame temporaries at
// BP-0C/BP-0E and the instruction count are part of the state the verifier compares).
#include "native.h"

namespace {

using namespace rc;

constexpr uint16_t RT = 0x19AE;
constexpr uint16_t kI = 0x3C, kN = 0x3B8, kR = 0x576, kC = 0x578, kK = 0x3BA;
constexpr uint16_t kRow = 0x38A, kCol = 0x39C, kX = 0x2F8, kY = 0x30A;  // array descriptors

uint16_t g(Cpu& c, uint16_t v) { return rd16(c, c.ds, v); }
void s(Cpu& c, uint16_t v, uint16_t x) { wr16(c, c.ds, v, x); }

// `mov si,desc; add bx,[si+0Ah]; mov es,[si+2]`: BX = element offset, ES = array segment.
void elem(Cpu& c, uint16_t desc) {
    c.si = desc;
    c.bx = add16(c, c.bx, g(c, uint16_t(desc + 0x0A)));
    c.es = g(c, uint16_t(desc + 2));
    c.icount += 3;
}

// Body shared by both passes up to the POINT call (AX = I on entry): R, C, then K = POINT(...) in AX.
void read_and_point(Cpu& c, uint16_t ret_ip) {
    c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.icount += 2;                      // I * 2
    elem(c, kRow);
    c.dx = c.ax; c.ax = rd16(c, c.es, c.bx); s(c, kR, c.ax); c.icount += 3;     // R = ROW(I)
    c.bx = c.dx; c.icount += 1;
    elem(c, kCol);
    c.ax = rd16(c, c.es, c.bx); s(c, kC, c.ax); c.icount += 2;                  // C = COL(I)
    c.bx = g(c, kR); c.bx = SHL16(c, c.bx, 1); c.icount += 2;
    c.si = kX; c.dx = c.bx; c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); c.icount += 2;
    c.cx = rd16(c, c.es, c.bx); c.cx = add16(c, c.cx, 6); push16(c, c.cx); c.icount += 3;     // X(R) + 6
    c.ax = SHL16(c, c.ax, 1); c.bx = c.ax; c.icount += 2;
    elem(c, kY);
    c.cx = rd16(c, c.es, c.bx); c.cx = add16(c, c.cx, 0x0B); push16(c, c.cx); c.icount += 3;  // Y(C) + 11
    wr16(c, c.ss, uint16_t(c.bp - 0x0C), c.ax);                                 // C * 2, kept for the PUT
    wr16(c, c.ss, uint16_t(c.bp - 0x0E), c.dx);                                 // R * 2
    c.icount += 3;
    rt_call_far(c, RT, 0x10EB, ret_ip);
}

// PUT (X(R), Y(C)), sprite, 3: coordinates from the BP temporaries, then the sprite array descriptor.
void put_sprite(Cpu& c, uint16_t sprite, uint16_t ret_coords, uint16_t ret_put) {
    c.si = kX; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0E)); c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kX + 0x0A)); c.es = g(c, kX + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    c.si = kY; c.bx = rd16(c, c.ss, uint16_t(c.bp - 0x0C)); c.icount += 2;
    c.bx = add16(c, c.bx, g(c, kY + 0x0A)); c.es = g(c, kY + 2); push16(c, rd16(c, c.es, c.bx)); c.icount += 3;
    c.icount += 1;
    rt_call_far(c, RT, 0x2AAB, ret_coords);
    push16(c, g(c, uint16_t(sprite + 2))); push16(c, g(c, sprite)); c.icount += 2;
    c.ax = sprite; push16(c, c.ax); c.ax = 3; push16(c, c.ax); c.icount += 4;
    c.icount += 1;
    rt_call_far(c, RT, 0x15A4, ret_put);
}

bool zf(const Cpu& c) { return (c.flags & F_ZF) != 0; }
bool greater(const Cpu& c) { return !zf(c) && (((c.flags & F_SF) != 0) == ((c.flags & F_OF) != 0)); }

// FOR loop head as compiled, block `test`: `mov [I],ax; cmp ax,[limit]; jg exit` then block `jmp body`.
// True: the body runs. The blocks are announced (rt_at) as the dispatcher would reach them.
bool for_test(Cpu& c, uint16_t limit, uint16_t test, uint16_t jmp, uint16_t body, uint16_t exit) {
    rt_at(c, test);
    s(c, kI, c.ax); (void)sub16(c, c.ax, g(c, limit)); c.icount += 3;
    if (greater(c)) { rt_at(c, exit); return false; }
    rt_at(c, jmp);
    c.icount += 1;
    rt_at(c, body);
    return true;
}

// `mov [K],ax; cmp ax,k; je then` then block `jmp else`. True: the THEN part runs.
bool k_equals(Cpu& c, uint16_t k, uint16_t jmp, uint16_t then) {
    s(c, kK, c.ax); (void)sub16(c, c.ax, k); c.icount += 3;
    if (zf(c)) { rt_at(c, then); return true; }
    rt_at(c, jmp);
    c.icount += 1;
    return false;
}

void scan_balloons(Cpu& c) {
    c.ax = g(c, kN); s(c, 0x574, c.ax); c.ax = 1; c.icount += 4;                // FOR I = 1 TO N
    while (for_test(c, 0x574, 0x595D, 0x5966, 0x588C, 0x5969)) {
        read_and_point(c, 0x58E9);
        if (k_equals(c, 13, 0x58F1, 0x58F4)) put_sprite(c, 0x1EA, 0x5917, 0x592C);
        else rt_at(c, 0x592C);
        // ROW(I) = R + 1: IF ROW(I) = 26 THEN ROW(I) = 1
        c.ax = g(c, kR); c.ax = inc16(c, c.ax); c.bx = g(c, kI); c.bx = SHL16(c, c.bx, 1); c.icount += 4;
        c.si = kRow; c.dx = c.bx; c.icount += 2;
        c.bx = add16(c, c.bx, g(c, kRow + 0x0A)); c.es = g(c, kRow + 2); wr16(c, c.es, c.bx, c.ax); c.icount += 3;
        (void)sub16(c, c.ax, 0x1A); c.icount += 2;
        if (zf(c)) {
            rt_at(c, 0x594C);
            c.bx = c.dx; c.bx = add16(c, c.bx, g(c, uint16_t(c.si + 0x0A))); c.es = g(c, uint16_t(c.si + 2));
            wr16(c, c.es, c.bx, 1); c.icount += 4;
        } else {
            rt_at(c, 0x5949);
            c.icount += 1;
        }
        rt_at(c, 0x5959);
        c.ax = g(c, kI); c.ax = inc16(c, c.ax); c.icount += 2;                   // NEXT I
    }
    c.ax = g(c, kN); s(c, 0x57A, c.ax); c.ax = 1; c.icount += 4;                // FOR I = 1 TO N
    while (for_test(c, 0x57A, 0x5A1A, 0x5A23, 0x5976, 0x5A26)) {
        read_and_point(c, 0x59D3);
        if (k_equals(c, 1, 0x59DB, 0x59DE)) put_sprite(c, 0x27A, 0x5A01, 0x5A16);
        else rt_at(c, 0x5A16);
        c.ax = g(c, kI); c.ax = inc16(c, c.ax); c.icount += 2;                   // NEXT I
    }
    c.icount += 1; c.ip = pop16(c);                                              // RETURN
}

rc::NativeReg reg{0x0824, 0x5880, false, &scan_balloons, "scan_balloons"};

}  // namespace
