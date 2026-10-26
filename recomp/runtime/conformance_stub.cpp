// Test harness for instruction conformance: runs one recompiled block on a caller-provided CPU state
// and memory, with devices stubbed. Built as a shared library and driven from tools/conformance.py,
// which runs the same bytes in Unicorn and compares the results.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "machine.h"

namespace rc {

static uint8_t* g_mem = nullptr;
static uint8_t plain_read(uint32_t a) { return g_mem[a]; }
static void plain_write(uint32_t a, uint8_t v) { g_mem[a] = v; }
MemRead g_vram_read = plain_read;
MemWrite g_vram_write = plain_write;
uint8_t* g_code_map = nullptr;
void rt_code_write(uint32_t, uint8_t) {}

struct Stop {};
static int g_event = 0;     // 0 none, 1 interrupt, 2 port access, 3 unsupported, 4 halt
static int g_event_arg = 0;

void rt_raise(Cpu& c, uint8_t n) { g_event = 1; g_event_arg = n; (void)c; }
uint8_t rt_in8(Cpu&, uint16_t p) { g_event = 2; g_event_arg = p; throw Stop{}; }
uint16_t rt_in16(Cpu&, uint16_t p) { g_event = 2; g_event_arg = p; throw Stop{}; }
void rt_out8(Cpu&, uint16_t p, uint8_t) { g_event = 2; g_event_arg = p; throw Stop{}; }
void rt_out16(Cpu&, uint16_t p, uint16_t) { g_event = 2; g_event_arg = p; throw Stop{}; }
void rt_halt(Cpu&) { g_event = 4; }
void rt_callback(Cpu&, uint16_t) { g_event = 3; throw Stop{}; }  // reference stubs: not an x86 instruction
void rt_unsupported(Cpu&, const char*) { g_event = 3; throw Stop{}; }

}  // namespace rc

using namespace rc;

struct State {  // flat layout shared with Python (ctypes)
    uint16_t ax, cx, dx, bx, sp, bp, si, di, cs, ds, es, ss, ip, flags;
    uint32_t icount;
};

extern "C" int rc_block_count() { return int(g_block_count); }
extern "C" void rc_block_at(int i, uint16_t* cs, uint16_t* ip, uint16_t* n) {
    *cs = g_blocks[i].cs; *ip = g_blocks[i].ip; *n = g_blocks[i].ninsn;
}

// Runs the block starting at state->cs:ip. Returns the event code (0 normal end, 1 INT n, 2 port I/O,
// 3 unsupported, 4 HLT, -1 no such block); *arg receives the interrupt number or port.
extern "C" int rc_run_block(State* s, uint8_t* mem, int* arg) {
    const BlockEntry* end = g_blocks + g_block_count;
    const BlockEntry* b = std::lower_bound(g_blocks, end, std::make_pair(s->cs, s->ip),
        [](const BlockEntry& e, const std::pair<uint16_t, uint16_t>& k) {
            return e.cs < k.first || (e.cs == k.first && e.ip < k.second);
        });
    if (b == end || b->cs != s->cs || b->ip != s->ip) return -1;
    Cpu c;
    c.ax = s->ax; c.cx = s->cx; c.dx = s->dx; c.bx = s->bx; c.sp = s->sp; c.bp = s->bp; c.si = s->si; c.di = s->di;
    c.cs = s->cs; c.ds = s->ds; c.es = s->es; c.ss = s->ss; c.ip = s->ip; c.flags = s->flags; c.mem = mem;
    g_mem = mem;
    g_event = 0;
    g_event_arg = 0;
    try {
        b->fn(c);
    } catch (Stop&) {
    }
    s->ax = c.ax; s->cx = c.cx; s->dx = c.dx; s->bx = c.bx; s->sp = c.sp; s->bp = c.bp; s->si = c.si; s->di = c.di;
    s->cs = c.cs; s->ds = c.ds; s->es = c.es; s->ss = c.ss; s->ip = c.ip; s->flags = c.flags;
    s->icount = uint32_t(c.icount);
    *arg = g_event_arg;
    return g_event;
}
