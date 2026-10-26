// Helpers for decompiled QuickBASIC 4.x code (native procedures, native.h). Each helper performs one
// statement pattern of the compiler's output with the same effects and instruction count, so procedures
// read like the BASIC source and stay cycle-exact:
//   argument push          mov ax,imm / push ax (0: xor ax,ax)      2 instructions per argument
//   runtime/SUB call       call far seg:off                          1
//   integer store          mov word [v],imm                          1
//   increment              inc word [v]                              1
//   IF v = k / v >= k      cmp word [v],k ; jcc short ; jmp near     2 when the jcc is taken, else 3
// Every original block start is announced (at): timers, IRQs, breakpoints and pokes happen there as in the
// recompiled code. Returns from calls are block starts and are announced by rt_call_far itself.
#pragma once
#include <initializer_list>

#include "native.h"

namespace rc::qb {

struct Qb {
    Cpu& c;
    // DGROUP integer variable at offset v (DS = DGROUP inside compiled code).
    uint16_t get(uint16_t v) const { return rd16(c, c.ds, v); }
    void set(uint16_t v, uint16_t value) { c.icount += 1; wr16(c, c.ds, v, value); }
    void inc(uint16_t v) { c.icount += 1; wr16(c, c.ds, v, inc16(c, get(v))); }
    // CALL FAR with arguments pushed in order (AX holds the last one, as after the compiler's sequence).
    void call(uint16_t seg, uint16_t off, std::initializer_list<uint16_t> args, uint16_t ret_ip) {
        for (uint16_t a : args) push_imm(a);
        c.icount += 1;
        rt_call_far(c, seg, off, ret_ip);
    }
    // IF tests on an integer variable, as `cmp word [v],k; jcc then; jmp else`: jmp_ip is the address of the
    // jmp (its own block), then_ip the jcc target. The else target is announced by the code that follows.
    bool eq(uint16_t v, uint16_t k, uint16_t jmp_ip, uint16_t then_ip) {
        return test(v, k, jmp_ip, then_ip, [this] { return (c.flags & F_ZF) != 0; });
    }
    bool ge(uint16_t v, uint16_t k, uint16_t jmp_ip, uint16_t then_ip) {
        return test(v, k, jmp_ip, then_ip, [this] { return ((c.flags & F_SF) != 0) == ((c.flags & F_OF) != 0); });
    }
    // Single instructions of the compiler's argument and call sequences.
    // mov ax,imm / push ax; the compiler loads 0 with xor ax,ax (which sets the flags).
    void push_imm(uint16_t v) {
        c.icount += 2;
        c.ax = v ? v : xor16(c, c.ax, c.ax);
        push16(c, c.ax);
    }
    void push_mem(uint16_t v) { c.icount += 1; push16(c, get(v)); }        // push word [v]
    void push_ax() { c.icount += 1; push16(c, c.ax); }                         // push ax (a function result)
    void push_ds() { c.icount += 1; push16(c, c.ds); }
    void xor_ax() { c.icount += 1; c.ax = xor16(c, c.ax, c.ax); }
    // Address of a string-array element pushed as an argument: `[mov si,desc;] mov bx,off; add bx,[si+0Ah];
    // push bx` (set_si: the compiler reloads SI, else SI still holds the descriptor).
    void push_elem_addr(uint16_t desc, uint16_t off, bool set_si) {
        if (set_si) { c.si = desc; c.icount += 1; }
        c.bx = off;
        c.bx = add16(c, c.bx, get(uint16_t(c.si + 0x0A)));
        push16(c, c.bx);
        c.icount += 3;
    }
    void callf(uint16_t seg, uint16_t off, uint16_t ret_ip) { c.icount += 1; rt_call_far(c, seg, off, ret_ip); }
    // INPUT #/LINE INPUT # style call: `mov ax,var; push ds; push ax; xor ax,ax; push ax; call`.
    void call_ds_var(uint16_t seg, uint16_t off, uint16_t var, uint16_t ret_ip) {
        c.icount += 1; c.ax = var; push_ds(); push16(c, c.ax); c.icount += 1; xor_ax(); push16(c, c.ax); c.icount += 1;
        callf(seg, off, ret_ip);
    }
    // After a runtime compare (flags from the callee): `je then; jmp else`.
    bool je(uint16_t jmp_ip, uint16_t then_ip) { return branch(jmp_ip, then_ip, (c.flags & F_ZF) != 0, 1); }
    // An original block start reached by a jump or fall-through (native.h rt_at): timers, IRQs, breakpoints,
    // pokes. Not needed at return points of calls (rt_call_far announces them).
    void at(uint16_t ip) { rt_at(c, ip); }
    // Calls whose return point the caller announces itself (generated region code: label A_xxxx / L_xxxx).
    void callf_x(uint16_t seg, uint16_t off, uint16_t ret_ip) { c.icount += 1; rt_call_far(c, seg, off, ret_ip, false); }
    void call_near_x(uint16_t off, uint16_t ret_ip) { c.icount += 1; rt_call_near(c, off, ret_ip, false); }
    // CALL near to another procedure of the module (GOSUB).
    void call_near(uint16_t off, uint16_t ret_ip) { c.icount += 1; rt_call_near(c, off, ret_ip); }
    // An inline 8087-emulator sequence (INT 34h-3Dh and the instructions around them) from `from` up to the
    // next statement at `to`: run as the original code (native.h rt_run).
    void fpu_seq(uint16_t from, uint16_t to) { rt_run(c, from, to); }
    // GOTO: jmp near.
    void jmp() { c.icount += 1; }
    void ret_near() { c.icount += 1; c.ip = pop16(c); }

private:
    template <typename F> bool test(uint16_t v, uint16_t k, uint16_t jmp_ip, uint16_t then_ip, F cond) {
        c.icount += 1;
        (void)sub16(c, get(v), k);
        return branch(jmp_ip, then_ip, cond(), 1);
    }
    // jcc (1) taken: the target block; else the jmp block and its jmp (1).
    bool branch(uint16_t jmp_ip, uint16_t then_ip, bool taken, int) {
        c.icount += 1;
        if (taken) { at(then_ip); return true; }
        at(jmp_ip);
        c.icount += 1;
        return false;
    }
};

}  // namespace rc::qb
