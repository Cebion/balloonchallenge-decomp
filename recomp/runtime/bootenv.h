// The reference machine captured at the program entry (see make_bootenv.py, refmachine.py).
#pragma once
#include <cstdint>

#include "machine_api.h"

namespace rc {

struct BootRegs {
    uint16_t ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, cs, ip, flags;
};

struct MemRegion {      // linear memory copied from the reference (low memory, ROMs, HMA)
    uint32_t linear;
    const uint8_t* data;
    uint32_t size;
};

struct CallbackVector {  // reference BIOS/DOS stub callback `FE 38 nn nn` -> the vector it serves
    uint16_t callback;
    uint8_t vector;
};

struct BootEnv {
    uint16_t psp_seg, env_seg, first_mcb;
    const MemRegion* regions;
    unsigned region_count;
    const CallbackVector* callbacks;
    unsigned callback_count;
    BootRegs regs;
    const VgaTiming* vga;   // display timing of the reference capture (refmachine.vga_timing)
    unsigned vga_count;
    double pit0_phase_ms;   // time since the PIT channel 0 period began, at the entry (refmachine.pit0_phase)
    uint16_t realint_seg, realint_off, realint_stop;  // interrupt trampoline (refmachine.realint_trampoline)
    uint16_t idle_seg, idle_off, idle_stop;           // idle routine (refmachine.idle_code)
};

extern const BootEnv kBootEnv;

}  // namespace rc
