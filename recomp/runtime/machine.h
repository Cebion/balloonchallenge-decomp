// Interface between recompiled blocks and the machine runtime (dispatcher, interrupts, devices).
#pragma once
#include <cstdint>

#include "cpu.h"

namespace rc {

using BlockFn = void (*)(Cpu&);
struct BlockEntry {
    uint16_t cs, ip;
    BlockFn fn;
    uint16_t ninsn;   // instructions in the block (conformance tests)
    uint16_t nbytes;  // code bytes the block was translated from (self-modifying-code detection)
};
extern const BlockEntry g_blocks[];   // generated (blocks.cpp), sorted by (cs, ip)
extern const unsigned g_block_count;
extern const uint16_t g_load_seg;      // segment the program image was translated for

// Software interrupt / exception n: c.ip must hold the return address. Pushes FLAGS, CS, IP,
// clears IF and TF, and continues at the vector from the interrupt table.
void rt_raise(Cpu& c, uint8_t n);
uint8_t rt_in8(Cpu& c, uint16_t port);
uint16_t rt_in16(Cpu& c, uint16_t port);
void rt_out8(Cpu& c, uint16_t port, uint8_t v);
void rt_out16(Cpu& c, uint16_t port, uint16_t v);
void rt_halt(Cpu& c);
// DOSBox-X callback opcode `FE 38 nn nn` in the reference BIOS/DOS stubs: c.ip holds the address after it.
// Runs the native service with DOSBox-X's semantics (flags into the stacked IRET frame, IP adjustments).
void rt_callback(Cpu& c, uint16_t number);
[[noreturn]] void rt_unsupported(Cpu& c, const char* what);

}  // namespace rc
