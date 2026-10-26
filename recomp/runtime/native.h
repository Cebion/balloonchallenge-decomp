// Hand-written (decompiled) procedures that replace recompiled ones (workflow/15-differential-decomp.md).
// A native procedure is entered where the recompiled one would be (return address already on the stack) and
// must leave exactly the same machine state: registers, flags, memory, devices and the instruction count
// (the clock), so CPU-paced behaviour stays identical. The runtime can check each call against the
// recompiled procedure (MachineOptions::native = Verify).
#pragma once
#include <cstdint>
#include <vector>

#include "cpu.h"

namespace rc {

using NativeFn = void (*)(Cpu&);
struct NativeProc {
    uint16_t cs, ip;   // entry
    bool far;          // entered by CALL FAR (returns with RETF), else near
    uint16_t begin, end;  // end 0: a procedure. Else a region of code that never returns (a GOTO-structured
                          // main program) entered at ip: blocks in [begin, end) of segment cs; the native code
                          // ends by setting CS:IP to the block where execution leaves the region.
    NativeFn fn;
    const char* name;
    bool verified;     // false: drafted, not yet equal over any recorded call; only Verify mode runs it
};
std::vector<NativeProc>& native_registry();
struct NativeReg {  // static registration: `static rc::NativeReg reg{0x0824, 0x5A27, false, &fn, "name"};`
    NativeReg(uint16_t cs, uint16_t ip, bool far, NativeFn fn, const char* name, bool verified = true) {
        native_registry().push_back({cs, ip, far, 0, 0, fn, name, verified});
    }
};
struct NativeRegion {  // `static rc::NativeRegion reg{0x0824, entry, begin, end, &fn, "name"};`
    NativeRegion(uint16_t cs, uint16_t ip, uint16_t begin, uint16_t end, NativeFn fn, const char* name, bool verified = true) {
        native_registry().push_back({cs, ip, false, begin, end, fn, name, verified});
    }
};

// Calls from native code into translated code. The caller has pushed the arguments; these push the return
// address the original CALL pushes and run until execution comes back to it with the stack above it.
// announce: the return point is announced as a block start (rt_at); generated region code announces at its
// labels instead and passes false.
void rt_call_far(Cpu& c, uint16_t seg, uint16_t off, uint16_t ret_ip, bool announce = true);
void rt_call_near(Cpu& c, uint16_t off, uint16_t ret_ip, bool announce = true);
// INT n as the original issues it (next_ip: the address after the 2-byte INT, pushed as the return address),
// continuing where the handler returns (cont_ip; the 8087-emulator interrupts return past their operand
// bytes).
void rt_int(Cpu& c, uint8_t n, uint16_t next_ip, uint16_t cont_ip);
// Runs the original code from from_ip until execution first reaches to_ip (same CS). Only for sequences
// that are library behaviour emitted inline: the 8087-emulator interrupt sequences (INT 34h-3Dh), whose
// handler patches its own call sites and chooses the return address. Documented exception to "a native
// procedure never runs the code it replaces" (workflow/15).
void rt_run(Cpu& c, uint16_t from_ip, uint16_t to_ip);
// Native code passing the start of an original block where the session may have a breakpoint or a poke
// (lockstep stops, test states): applies them as the dispatcher would at that block.
void rt_at(Cpu& c, uint16_t ip);

}  // namespace rc
