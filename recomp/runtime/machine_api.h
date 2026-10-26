// Host-facing API of the recompiled DOS machine (used by the SDL2 frontend, headless runner, lockstep).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rc {

// Display timing of the reference machine (DOSBox-X REA_LOG_VGA, converted by refmachine.vga_timing): from
// `from_ms` on, frames start on the grid t0_ms + n * vtotal and port 3DA reports blanking/retrace within each
// frame as DOSBox-X vga_read_p3da does. Times in ms of emulated time since program entry.
struct VgaTiming { double from_ms, t0_ms, vtotal, htotal, hblkstart, hblkend, vrstart, vrend, vdend; };

struct MachineOptions {
    std::string data_dir;             // DOS drive C: root (the game's own files)
    uint32_t start_ticks = 0x16C627;  // BIOS tick count at program start (reference capture default)
    double instructions_per_ms = 8000.0;  // CPU speed; DOSBox-X cycles=fixed 8000 (normal core: ~1 cycle/insn)
    std::string missing_log;          // append untranslated CS:IP targets here (discovery runs)
    bool trace_ints = false;          // log every software interrupt to stderr
    std::vector<uint32_t> tick_overrides;  // optional: BIOS tick values to return, in order, at INT 1Ah AH=0
    // Optional clock input replay: DOS get-time (INT 21h AH=2Ch) results recorded from the reference
    // machine, in order, with the caller CS:IP each was read from (a different caller stops the run).
    struct DosTime { uint32_t caller; uint16_t cx, dx; };
    std::vector<DosTime> dos_time_replay;
    // Optional speaker I/O log (lockstep): PIT channel 2 data/mode and port 61h writes, as lines
    // "PP VV ms" like the instrumented DOSBox-X (REA_LOG_SPEAKER).
    std::string speaker_io_log;
    // Optional one-shot memory writes (lockstep test states, e.g. a forced registration flag): when
    // execution first reaches block `at` (CS:IP), the word `value` is written at linear `addr`. The golden
    // capture does the same with a debugger breakpoint and SM.
    // Display timing (see VgaTiming). Empty: the reference capture's timing in the boot environment.
    std::vector<VgaTiming> vga_timing;
    // Phase of the BIOS timer tick at the entry: ms since the PIT channel 0 period began (refmachine.pit0_phase).
    // Negative: the reference capture's (kBootEnv.pit0_phase_ms).
    double pit0_phase_ms = -1;
    // Emulated disk transfer rate of the reference (DOSBox-X "hard drive data rate limit" = -1 with the PCI
    // bus enabled: 8333333 bytes/s). File I/O spends bytes/rate in the reference's idle routine.
    double disk_bytes_per_s = 8333333.0;
    // Decompiled procedures (native.h): Off runs the recompiled code everywhere; On runs the native
    // procedures; Verify runs both for every call (the recompiled one in a forked copy of the machine) and
    // stops at the first difference in registers, instruction count, memory, video planes or port writes.
    enum class Native { Off, On, Verify } native = Native::Off;
    // Optional stop handler: called synchronously when execution reaches a breakpoint (also inside calls
    // made from native procedures, which cannot return to the host mid-procedure); the run then continues
    // unless the handler calls machine_stop. Without it a breakpoint ends machine_run_ms with Break.
    // primary is false in the forked copy of a native verification: apply the same inputs (keys, clock),
    // write nothing.
    std::function<void(bool primary)> on_break;
    struct Poke { uint32_t at; uint32_t addr; uint16_t value; };
    std::vector<Poke> pokes;
};

enum class RunState { Running, Exited, Missing, Unsupported, Halted, Break };

class Machine;
Machine* machine_create();
void machine_destroy(Machine*);
bool machine_init(Machine*, const MachineOptions&, std::string& error);
// Run until the emulated clock advances by `ms` milliseconds or the program stops.
RunState machine_run_ms(Machine*, double ms);
RunState machine_state(const Machine*);
// Ends the run at the next block boundary (state Halted), e.g. from an on_break handler.
void machine_stop(Machine* m);
// Verified calls per decompiled procedure (MachineOptions::Native::Verify), as "name=count ...".
std::string machine_native_report(const Machine* m);
std::string machine_message(const Machine*);   // why it stopped (exit code, missing target, ...)
void machine_key(Machine*, uint8_t scancode);  // make/break codes (bit 7 = release), queued
void machine_render(const Machine*, std::vector<uint8_t>& indices, int& w, int& h, uint32_t rgb[16]);
// Speaker: (instruction count, frequency Hz or 0 = off) events since the last call.
std::vector<std::pair<uint64_t, uint32_t>> machine_take_speaker(Machine*);
uint64_t machine_icount(const Machine*);
// Debug: count executed blocks per CS:IP while enabled; returns the top entries ("SSSS:OOOO count").
void machine_profile(Machine*, bool enable);
std::vector<std::pair<uint32_t, uint64_t>> machine_profile_top(const Machine*, size_t n);
const uint8_t* machine_memory(const Machine*);        // 1 MB + 64 KB linear memory
// Lockstep support. A breakpoint stops the run (RunState::Break) before the block at CS:IP executes
// (the address must be a block start: seed it in entries.txt); the next run continues from there.
void machine_add_breakpoint(Machine*, uint16_t cs, uint16_t ip);
uint32_t machine_cs_ip(const Machine*);
uint32_t machine_ss_sp(const Machine*);
uint8_t* machine_memory_rw(Machine*);                 // host writes while stopped (BDA key/tick injection)
void machine_out8(Machine*, uint16_t port, uint8_t v);  // host port writes (mirror debugger side effects)
const uint8_t* machine_plane(const Machine*, int p);  // EGA planes 0-3 (64 KB each)

}  // namespace rc
