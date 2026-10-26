// Recompiled-DOS machine: dispatcher, interrupts, PIC/PIT/keyboard/speaker, BIOS and DOS services.
// The program's own code (including its compiler runtime) runs as recompiled blocks; this file supplies
// only what a PC's hardware, BIOS and DOS would. The reference behaviour is DOSBox-X with the pinned
// configuration (machine=svga_s3, cputype=386, cycles=fixed 8000, fpu/ems/xms/umb/int33 off).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bootenv.h"
#include "ega.h"
#include "machine.h"
#include "native.h"
#include "machine_api.h"

namespace rc {

constexpr double kPitHz = 1193182.0;

class Machine {
public:
    Cpu c;
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x110000 + 16, 0);
    Ega ega;
    MachineOptions opt;
    std::unordered_map<uint32_t, BlockFn> blocks;
    // Direct-mapped cache in front of `blocks` (filled once at init, never changed): CS:IP -> block.
    struct BlockCacheEntry { uint32_t key; BlockFn fn; };
    static constexpr size_t kBlockCacheSize = 1 << 14;
    std::vector<BlockCacheEntry> block_cache = std::vector<BlockCacheEntry>(kBlockCacheSize, {0, nullptr});
    RunState state = RunState::Running;
    std::string message;
    // PIC
    uint8_t pic_imr = 0xF8,  // reference at entry: IRQ0-2 enabled (the runtime saves it: DGROUP 480B)
         pic_irr = 0, pic_isr = 0;
    // PIT
    struct Channel {
        uint16_t reload = 0; uint8_t mode = 3, access = 3; bool low_next = true; uint16_t latch = 0; bool latched = false;
        bool latch_low_next = true; int64_t start_icount = 0;
        // 8254 (and DOSBox-X): a count written in mode 2/3 without a new mode word is loaded at the end of
        // the current period; after a mode word (or in modes 0/4) it starts at once.
        // new_mode starts false: the reference BIOS programmed the PIT before the program ran.
        bool new_mode = false; bool pending = false; uint16_t next_reload = 0; uint16_t write = 0;
    };
    Channel pit[3];
    double next_irq0_icount = 0;
    uint8_t port61 = 0;
    uint32_t speaker_freq = 0;
    std::vector<std::pair<uint64_t, uint32_t>> speaker_events;
    // keyboard
    std::deque<uint8_t> key_queue;
    uint8_t port60 = 0;
    bool kbd_full = false;
    bool e0_prefix = false;
    // DOS
    struct File { FILE* f = nullptr; std::string path; bool device = false; };
    std::vector<File> files = std::vector<File>(20);
    uint16_t dta_seg = 0, dta_off = 0x80;
    size_t tick_override_pos = 0;
    size_t dos_time_pos = 0;
    std::FILE* missing = nullptr;
    std::FILE* speaker_io = nullptr;
    bool profiling = false;
    std::vector<uint8_t> code_map;
    std::unordered_set<uint32_t> smc_seen;
    std::unordered_set<uint32_t> smc_unhandled;  // modified translated bytes without a handled pattern
    std::unordered_map<uint32_t, uint16_t> block_bytes;
    uint32_t cur_block = 0;
    std::unordered_map<uint32_t, uint64_t> profile;
    std::unordered_set<uint32_t> breakpoints;
    uint64_t resume_addr = ~0ull;  // breakpoint address to pass once after a Break
    std::vector<MachineOptions::Poke> pokes;  // pending one-shot writes (opt.pokes)
    unsigned nesting = 0;         // nested run depth (real_int, idle, calls from native procedures)
    std::unordered_map<uint32_t, NativeProc> natives;  // entry CS:IP -> decompiled procedure (opt.native)
    bool io_log_on = false;       // port writes of the current verified call
    std::vector<uint64_t> io_log; // (icount << 32 | port << 16 | value)
    uint16_t con_binary = 0;      // CON raw mode bit (IOCTL 4401h DX bit 5), as DOSBox-X device_CON::binary
    uint64_t realint_stops = 0;   // trampoline stop callbacks executed
    FILE* block_trace = std::getenv("RC_TRACE_BLOCKS") ? std::fopen(std::getenv("RC_TRACE_BLOCKS"), "w") : nullptr;
    double block_trace_ms = std::getenv("RC_TRACE_MS") ? std::atof(std::getenv("RC_TRACE_MS")) : 0;
    uint32_t block_trace_at = std::getenv("RC_TRACE_AT") ? uint32_t(std::strtoul(std::getenv("RC_TRACE_AT"), nullptr, 16) << 16 |
                                                                     std::strtoul(std::getenv("RC_TRACE_AT") + 5, nullptr, 16)) : 0;
    double block_trace_from = std::getenv("RC_TRACE_FROM_MS") ? std::atof(std::getenv("RC_TRACE_FROM_MS")) : -1;
    std::vector<VgaTiming> vga;  // display timing (opt.vga_timing or the boot environment's)

    double ipms() const { return opt.instructions_per_ms; }
    double icount_per_pit_tick() const { return ipms() * 1000.0 / kPitHz; }

    // ---------------- memory helpers ----------------
    uint8_t& m8(uint32_t a) { return mem[a & 0x1FFFFF]; }
    uint16_t m16(uint32_t a) { return uint16_t(mem[a] | (mem[a + 1] << 8)); }
    void s16(uint32_t a, uint16_t v) { mem[a] = uint8_t(v); mem[a + 1] = uint8_t(v >> 8); }
    uint32_t L(uint16_t s, uint16_t o) const { return (uint32_t(s) << 4) + o; }

    // ---------------- interrupts ----------------
    void raise(uint8_t n) {
        push16(c, get_flags(c));
        push16(c, c.cs);
        push16(c, c.ip);
        setf(c, F_IF | F_TF, false);
        c.ip = m16(n * 4u);
        c.cs = m16(n * 4u + 2);
    }
    // Flags a callback returns: written into the stacked IRET frame (SS:SP+4), as DOSBox-X CALLBACK_SCF/SZF.
    void cb_flags(uint16_t keep = F_CF | F_ZF) {
        const uint32_t a = L(c.ss, uint16_t(c.sp + 4));
        s16(a, uint16_t((m16(a) & ~keep) | (c.flags & keep)));
    }
    void service_irqs() {
        if (!(c.flags & F_IF)) return;
        const uint8_t pending = pic_irr & uint8_t(~pic_imr);
        if (!pending) return;
        for (int irq = 0; irq < 8; ++irq) {
            if (pic_isr & (1 << irq)) return;  // higher-priority interrupt in service
            if (pending & (1 << irq)) {
                pic_irr &= uint8_t(~(1 << irq));
                pic_isr |= uint8_t(1 << irq);
                raise(uint8_t(8 + irq));
                return;
            }
        }
    }
    void update_timers() {
        while (double(c.icount) >= next_irq0_icount) {
            pic_irr |= 1;
            if (pit[0].pending) {  // period end: the deferred count takes effect
                pit[0].reload = pit[0].next_reload;
                pit[0].pending = false;
                pit[0].start_icount = int64_t(next_irq0_icount);
            }
            const unsigned period = pit[0].reload ? pit[0].reload : 65536u;
            next_irq0_icount += period * icount_per_pit_tick();
        }
        if (pit[2].pending) {  // speaker channel: deferred count at the end of its current period
            const unsigned period = pit[2].reload ? pit[2].reload : 65536u;
            const double end = double(pit[2].start_icount) + period * icount_per_pit_tick();
            if (double(c.icount) >= end) {
                pit[2].reload = pit[2].next_reload;
                pit[2].pending = false;
                pit[2].start_icount = int64_t(end);
                speaker_update();
            }
        }
        if (!kbd_full && !key_queue.empty() && !(pic_isr & 2) && !(pic_irr & 2)) {
            port60 = key_queue.front();
            key_queue.pop_front();
            kbd_full = true;
            pic_irr |= 2;
        }
    }
    // Port 3DA status from the display timing in effect (DOSBox-X vga_read_p3da): bit 0 blanking (vertical
    // from display end, else horizontal within the line), bit 3 vertical retrace.
    uint8_t vga_status() const {
        const double t = double(c.icount) / ipms();
        const VgaTiming* v = &vga[0];
        for (const auto& e : vga) if (e.from_ms <= t) v = &e;
        const double f = std::fmod(t - v->t0_ms, v->vtotal);
        const double in_frame = f < 0 ? f + v->vtotal : f;
        uint8_t r = 0;
        if (in_frame >= v->vdend) r |= 1;
        else {
            const double in_line = std::fmod(in_frame, v->htotal);
            if (in_line >= v->hblkstart && in_line <= v->hblkend) r |= 1;
        }
        if (in_frame >= v->vrstart && in_frame <= v->vrend) r |= 8;
        return r;
    }

    // One translated block, with timer and IRQ delivery before it; false when the run stops (breakpoint, missing
    // or modified code). Breakpoints apply to the program's own execution, not to nested interrupts (real_int).
    bool step() {
        update_timers();
        service_irqs();
        const uint32_t addr = (uint32_t(c.cs) << 16) | c.ip;
        BlockCacheEntry& cached = block_cache[(addr ^ (addr >> 14)) & (kBlockCacheSize - 1)];
        if (cached.key != addr || !cached.fn) {
            auto found = blocks.find(addr);
            if (found == blocks.end()) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "no translated code for %04X:%04X", c.cs, c.ip);
                message = buf;
                if (missing) { std::fprintf(missing, "%04X:%04X\n", c.cs, c.ip); std::fflush(missing); }
                state = RunState::Missing;
                return false;
            }
            cached = {addr, found->second};
        }
        const BlockFn fn = cached.fn;  // the slot may be reused by nested runs below
        if (!at_block(addr)) return false;
        if (block_trace) {  // RC_TRACE_BLOCKS: block start and the clock as DOSBox-X counts the instruction, for
            // RC_TRACE_MS ms from the first time block RC_TRACE_AT (SSSS:OOOO, default: the start) runs
            const double ms = double(c.icount + 1) / ipms();
            if (block_trace_from < 0 && (!block_trace_at || block_trace_at == addr)) block_trace_from = ms;
            if (block_trace_from >= 0 && ms >= block_trace_from && ms <= block_trace_from + block_trace_ms)
                std::fprintf(block_trace, "%04X:%04X %.6f\n", c.cs, c.ip, ms);
        }
        if (!natives.empty() && opt.native != MachineOptions::Native::Off && !natives_suspended) {
            const auto n = natives.find(addr);
            if (n != natives.end()) {
                cur_block = addr;
                ++in_native;
                if (opt.native == MachineOptions::Native::Verify && nesting == 0) verify_native(n->second);
                else { n->second.fn(c); if (!verify_child) ++nested_runs[n->second.name]; }
                --in_native;
                return true;
            }
        }
        if (profiling) ++profile[addr];
        cur_block = addr;
        if (!smc_unhandled.empty()) {
            const uint32_t start = (uint32_t(c.cs) << 4) + c.ip, len = block_bytes[addr];
            for (uint32_t a : smc_unhandled)
                if (a >= start && a < start + len) {
                    char buf[120];
                    std::snprintf(buf, sizeof buf, "executing modified code at %05X (block %04X:%04X)", a, c.cs, c.ip);
                    message = buf;
                    state = RunState::Unsupported;
                    return false;
                }
        }
        fn(c);
        return true;
    }

    // Breakpoints and pokes of the block at addr (also announced by native code, rt_at); false: the run stops.
    bool at_block(uint32_t addr) {
        if (!breakpoints.empty() && breakpoints.count(addr)) {
            if (opt.on_break) {
                if (break_seen != addr || break_icount != c.icount) {  // once per arrival
                    break_seen = addr; break_icount = c.icount;
                    opt.on_break(!verify_child);
                    if (state != RunState::Running) return false;
                }
            } else if (nesting > 0 || in_native) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "breakpoint %04X inside a native procedure needs on_break", unsigned(addr & 0xFFFF));
                message = buf;
                state = RunState::Unsupported;
                return false;
            } else {
                if (resume_addr != addr) { state = RunState::Break; return false; }
                resume_addr = ~0ull;
            }
        }
        for (size_t i = 0; i < pokes.size(); ++i)
            if (pokes[i].at == addr) {
                m8(pokes[i].addr) = uint8_t(pokes[i].value);
                m8(pokes[i].addr + 1) = uint8_t(pokes[i].value >> 8);
                pokes.erase(pokes.begin() + i--);
            }
        return true;
    }

    // An interrupt the reference's own native DOS/BIOS code issues (DOSBox-X CALLBACK_RunRealInt): the
    // reference trampoline `INT n; stop callback` runs as real code, through the vector, with its cycles,
    // timers and IRQs, then the caller's CS:IP is restored. Results are in the registers and flags.
    void real_int(uint8_t n) { run_nested(kBootEnv.realint_seg, uint16_t(kBootEnv.realint_off + n * 6)); }

    // Reference code at seg:off that ends in a stop callback (interrupt trampoline, idle routine), run nested
    // with the caller's CS:IP restored afterwards.
    void run_nested(uint16_t seg, uint16_t off) {
        const uint16_t cs0 = c.cs, ip0 = c.ip;
        const uint64_t stops0 = realint_stops;
        c.cs = seg;
        c.ip = off;
        ++nesting;
        while (realint_stops == stops0)
            if (!step()) throw state;
        --nesting;
        c.cs = cs0;
        c.ip = ip0;
    }

    // Native code at the start of original block ip (same CS): what the dispatcher does before a block,
    // timers, then a pending IRQ (its handler runs nested and returns to ip), then breakpoints and pokes.
    void native_block(uint16_t ip) {
        c.ip = ip;
        const uint16_t cs0 = c.cs, sp0 = c.sp;
        for (;;) {  // as consecutive dispatcher steps: another IRQ may be taken before the block runs
            update_timers();
            service_irqs();
            if (c.cs == cs0 && c.ip == ip) break;
            run_until(cs0, ip, uint16_t(sp0 - 1));
        }
        if (block_trace && block_trace_from >= 0) {  // same lines as step(): comparable with a recompiled run
            const double ms = double(c.icount + 1) / ipms();
            if (ms >= block_trace_from && ms <= block_trace_from + block_trace_ms) std::fprintf(block_trace, "%04X:%04X %.6f\n", c.cs, ip, ms);
        }
        if (!at_block((uint32_t(c.cs) << 16) | ip)) throw state;
    }

    // Run until execution is back at ret_cs:ret_ip with the stack above sp_ret (the slot of the return
    // address): the end of a call made from native code, or of the procedure a verification runs.
    // any_sp: the end of a call is the first arrival at the return address, whatever SP is: runtime helpers
    // that set up the caller's stack frame (QuickBASIC SUB prologue 19AE:2883) return below the slot of
    // their return address.
    void run_until(uint16_t ret_cs, uint16_t ret_ip, uint16_t sp_ret, bool any_sp = false) {
        ++nesting;
        while (!(c.cs == ret_cs && c.ip == ret_ip && (any_sp || c.sp > sp_ret)))
            if (!step()) throw state;
        --nesting;
    }

    // One call of a native procedure, checked against the recompiled procedure: a forked copy of the machine
    // runs the recompiled code to the return and sends its state; this process runs the native code and
    // compares. The first difference stops the run (state Unsupported, message names it).
    // The recompiled side of a region: run until a block of the region's segment outside [ip, end) at the
    // region's stack depth or above (calls made inside the region return before that).
    // Bounded (the verified side must leave the region about where the native side did): a run that stays
    // inside for 2e9 instructions is reported as a difference instead of blocking the verification.
    void run_region_out(const NativeProc& p) {
        const uint16_t sp0 = c.sp;
        const uint64_t limit = c.icount + 2000000000ull;
        ++nesting;
        do {
            if (!step() || c.icount > limit) throw state;
        } while (!(c.cs == p.cs && c.sp >= sp0 && (c.ip < p.begin || c.ip >= p.end)));
        --nesting;
    }

    void verify_native(const NativeProc& p) {
        const uint16_t sp_ret = c.sp;
        const uint16_t ret_ip = p.end ? 0 : rd16(c, c.ss, c.sp), ret_cs = p.far ? rd16(c, c.ss, uint16_t(c.sp + 2)) : c.cs;
        int fd[2];
        if (pipe(fd) != 0) { message = "verify: pipe failed"; state = RunState::Unsupported; throw state; }
        std::fflush(nullptr);
        const pid_t pid = fork();
        if (pid == 0) {
            close(fd[0]);
            speaker_io = nullptr; missing = nullptr; block_trace = nullptr;  // the parent owns the log files
            io_log_on = true; io_log.clear();
            bool ok = true;
            try {
                natives_suspended = true;
                verify_child = true;
                if (p.end) run_region_out(p); else run_until(ret_cs, ret_ip, sp_ret);
            } catch (RunState) { ok = false; }
            send_state(fd[1], ok);
            _exit(0);
        }
        close(fd[1]);
        io_log_on = true; io_log.clear();
        p.fn(c);
        io_log_on = false;
        const std::string diff = compare_state(fd[0]);
        close(fd[0]);
        waitpid(pid, nullptr, 0);
        if (!diff.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "native %s (%04X:%04X) differs: ", p.name, p.cs, p.ip);
            message = buf + diff;
            state = RunState::Unsupported;
            throw state;
        }
        ++verified_calls[p.name];
    }
    bool natives_suspended = false;   // run the original code (verification side, rt_run sequences)
    bool verify_child = false;        // this process is the forked recompiled side of a verification
    unsigned in_native = 0;           // native procedures running (their breakpoints need on_break)
    uint32_t break_seen = 0; uint64_t break_icount = ~0ull;  // on_break: the arrival already handled   // the forked recompiled side of a verification
    std::unordered_map<std::string, uint64_t> verified_calls;
    std::unordered_map<std::string, uint64_t> nested_runs;  // native units run inside a verified unit (covered by its check)

    static void write_all(int fd, const void* p, size_t n) {
        const char* b = static_cast<const char*>(p);
        while (n) { const ssize_t w = write(fd, b, n); if (w <= 0) return; b += w; n -= size_t(w); }
    }
    static bool read_all(int fd, void* p, size_t n) {
        char* b = static_cast<char*>(p);
        while (n) { const ssize_t r = read(fd, b, n); if (r <= 0) return false; b += r; n -= size_t(r); }
        return true;
    }
    struct RegState { uint16_t r[14]; uint64_t icount; };
    RegState regs() const {
        return {{c.ax, c.bx, c.cx, c.dx, c.si, c.di, c.bp, c.sp, c.cs, c.ds, c.es, c.ss, c.ip, c.flags}, c.icount};
    }
    void send_state(int fd, bool ok) {
        const RegState r = regs();
        const uint8_t okb = ok;
        write_all(fd, &okb, 1);
        write_all(fd, &r, sizeof r);
        write_all(fd, mem.data(), 0x110000);
        write_all(fd, ega.planes, sizeof ega.planes);
        const uint64_t n = io_log.size();
        write_all(fd, &n, sizeof n);
        write_all(fd, io_log.data(), n * sizeof(uint64_t));
    }
    std::string compare_state(int fd) {
        uint8_t okb = 0; RegState r{};
        static std::vector<uint8_t> m(0x110000), pl(sizeof(Ega::planes));
        uint64_t n = 0;
        if (!read_all(fd, &okb, 1) || !read_all(fd, &r, sizeof r) || !read_all(fd, m.data(), m.size()) ||
            !read_all(fd, pl.data(), pl.size()) || !read_all(fd, &n, sizeof n))
            return "recompiled side sent no state";
        std::vector<uint64_t> io(n);
        if (!read_all(fd, io.data(), n * sizeof(uint64_t))) return "recompiled side sent no port log";
        if (!okb) {
            char b2[96];
            std::snprintf(b2, sizeof b2, "recompiled side did not reach the end (it stopped at %04X:%04X)", r.r[8], r.r[12]);
            return b2;
        }
        static const char* names[14] = {"AX", "BX", "CX", "DX", "SI", "DI", "BP", "SP", "CS", "DS", "ES", "SS", "IP", "FLAGS"};
        const RegState mine = regs();
        char buf[160];
        for (int i = 0; i < 14; ++i)
            if (mine.r[i] != r.r[i]) { std::snprintf(buf, sizeof buf, "%s native %04X recompiled %04X", names[i], mine.r[i], r.r[i]); return buf; }
        if (mine.icount != r.icount) {
            std::snprintf(buf, sizeof buf, "instruction count native %llu recompiled %llu", (unsigned long long)mine.icount, (unsigned long long)r.icount);
            return buf;
        }
        for (size_t a = 0; a < m.size(); ++a)
            if (mem[a] != m[a]) { std::snprintf(buf, sizeof buf, "memory %05zX native %02X recompiled %02X", a, mem[a], m[a]); return buf; }
        if (std::memcmp(pl.data(), ega.planes, pl.size()) != 0) return "video planes";
        if (io != io_log) {
            std::snprintf(buf, sizeof buf, "port writes (%zu native, %zu recompiled)", io_log.size(), io.size());
            return buf;
        }
        return "";
    }

    // DOSBox-X CALLBACK_Idle: the idle routine with interrupts enabled, IF restored after.
    void idle() {
        const bool if0 = c.flags & F_IF;
        setf(c, F_IF, true);
        run_nested(kBootEnv.idle_seg, kBootEnv.idle_off);
        setf(c, F_IF, if0);
    }

    // DOSBox-X diskio_delay (hard drive): file I/O takes bytes / rate of emulated time, spent idling with
    // interrupts enabled (CPU_STI).
    void diskio_delay(double bytes) {
        const double end = double(c.icount) / ipms() + bytes / opt.disk_bytes_per_s * 1000.0;
        setf(c, F_IF, true);
        do idle(); while (double(c.icount) / ipms() < end);
    }

    // CON device information (DOSBox-X device_CON::GetInformation): input status from INT 16h AH=11h run as
    // real code; EOF-on-input (bit 6) when no key is waiting. AX is preserved.
    uint16_t con_information() {
        const uint16_t ax0 = c.ax;
        uint16_t ret = 0x93 | 0x40;
        c.ah = 0x11;
        real_int(0x16);
        if (!(c.flags & F_ZF)) ret = 0x93;
        c.ax = ax0;
        return uint16_t(ret | con_binary);
    }

    // ---------------- ports ----------------
    uint16_t pit_count(int ch) {
        const unsigned period = pit[ch].reload ? pit[ch].reload : 65536u;
        const double ticks = (double(c.icount) - double(pit[ch].start_icount)) / icount_per_pit_tick();
        unsigned elapsed = unsigned(uint64_t(ticks) % period);
        unsigned count = period - elapsed;
        if (pit[ch].mode == 3) count = (count * 2) % period;  // square wave counts down by 2
        return uint16_t(count);
    }
    uint8_t in8(uint16_t port) {
        if ((port >= 0x3C0 && port <= 0x3CF) || (port >= 0x3D4 && port <= 0x3DA)) return ega.port_read(port, port == 0x3DA ? vga_status() : 0);
        switch (port) {
            case 0x20: return pic_irr;
            case 0x21: return pic_imr;
            case 0x40: case 0x41: case 0x42: {
                Channel& ch = pit[port - 0x40];
                const uint16_t v = ch.latched ? ch.latch : pit_count(port - 0x40);
                uint8_t r;
                if ((ch.access & 3) == 1) r = uint8_t(v);
                else if ((ch.access & 3) == 2) r = uint8_t(v >> 8);
                else { r = ch.latch_low_next ? uint8_t(v) : uint8_t(v >> 8); ch.latch_low_next = !ch.latch_low_next; if (ch.latch_low_next) ch.latched = false; return r; }
                ch.latched = false;
                return r;
            }
            case 0x60: kbd_full = false; return port60;
            case 0x61: return uint8_t((port61 & 0x0F) | ((uint64_t(double(c.icount) / (ipms() / 66.0)) & 1) ? 0x10 : 0) | 0x20);
            case 0x64: return uint8_t(kbd_full ? 0x1D : 0x1C);
            default: return 0xFF;
        }
    }
    void speaker_update() {
        const uint32_t f = ((port61 & 3) == 3 && pit[2].reload != 1) ? uint32_t(kPitHz / (pit[2].reload ? pit[2].reload : 65536)) : 0;
        if (f != speaker_freq) { speaker_freq = f; speaker_events.push_back({c.icount, f}); }
    }
    void out8(uint16_t port, uint8_t v) {
        if ((port >= 0x3C0 && port <= 0x3CF) || (port >= 0x3D4 && port <= 0x3DA)) { ega.port_write(port, v); return; }
        if (speaker_io && (port == 0x42 || port == 0x61 || (port == 0x43 && (v >> 6) == 2)))
            std::fprintf(speaker_io, "%02X %02X %.3f %04X:%04X\n", port, v, double(c.icount) / ipms(), cur_block >> 16, cur_block & 0xFFFF);
        switch (port) {
            case 0x20:
                if ((v & 0xE0) == 0x20) {  // EOI: clear highest-priority in-service bit
                    for (int i = 0; i < 8; ++i) if (pic_isr & (1 << i)) { pic_isr &= uint8_t(~(1 << i)); break; }
                }
                break;
            case 0x21: pic_imr = v; break;
            case 0x43: {
                const int ch = v >> 6;
                if (ch == 3) break;
                if ((v & 0x30) == 0) { pit[ch].latch = pit_count(ch); pit[ch].latched = true; pit[ch].latch_low_next = true; break; }
                pit[ch].access = (v >> 4) & 3;
                pit[ch].mode = (v >> 1) & 7;
                pit[ch].low_next = true;
                pit[ch].new_mode = true;
                pit[ch].pending = false;
                break;
            }
            case 0x40: case 0x41: case 0x42: {
                const int ch = port - 0x40;
                Channel& p = pit[ch];
                uint16_t& w = p.write;  // count being assembled (lsb/msb), as the 8254 write latch
                if (p.access == 1) w = uint16_t(v);
                else if (p.access == 2) w = uint16_t(v << 8);
                else {
                    if (p.low_next) w = uint16_t((w & 0xFF00) | v);
                    else w = uint16_t((w & 0x00FF) | (v << 8));
                    p.low_next = !p.low_next;
                    if (!p.low_next) break;
                }
                const uint8_t mode = p.mode & 3;  // modes 6/7 are 2/3
                if (!p.new_mode && (mode == 2 || mode == 3)) {
                    p.next_reload = w;
                    p.pending = true;
                    break;
                }
                p.new_mode = false;
                p.pending = false;
                p.reload = w;
                p.start_icount = c.icount;
                if (ch == 0) next_irq0_icount = double(c.icount) + (p.reload ? p.reload : 65536u) * icount_per_pit_tick();
                if (ch == 2) speaker_update();
                break;
            }
            case 0x61: port61 = v; speaker_update(); break;
            default: break;
        }
    }

    // ---------------- BIOS ----------------
    uint8_t* bda() { return &mem[0x400]; }
    uint16_t& bda16(unsigned off) { return *reinterpret_cast<uint16_t*>(&mem[0x400 + off]); }
    void kbd_put(uint16_t word) {
        uint16_t head = bda16(0x1A), tail = bda16(0x1C);
        uint16_t next = uint16_t(tail + 2);
        if (next >= bda16(0x82)) next = bda16(0x80);
        if (next == head) return;  // buffer full
        s16(0x400 + tail, word);
        bda16(0x1C) = next;
    }
    bool kbd_peek(uint16_t& word) {
        if (bda16(0x1A) == bda16(0x1C)) return false;
        word = m16(0x400 + bda16(0x1A));
        return true;
    }
    void kbd_pop() {
        uint16_t head = uint16_t(bda16(0x1A) + 2);
        if (head >= bda16(0x82)) head = bda16(0x80);
        bda16(0x1A) = head;
    }
    void int09();
    void int10();
    // BIOS text output (INT 10h AH=06-0Eh) in text and planar graphics modes
    unsigned rows() { return unsigned(mem[0x484]) + 1; }
    unsigned cols() { return bda16(0x4A); }
    unsigned char_h() { return bda16(0x85); }
    void put_pixel(unsigned x, unsigned y, uint8_t color, bool xor_) {
        const unsigned off = y * 80 + (x >> 3);
        const uint8_t bit = uint8_t(0x80 >> (x & 7));
        for (int p = 0; p < 4; ++p) {
            uint8_t& b = ega.planes[p][off & 0xFFFF];
            const bool on = (color >> p) & 1;
            if (xor_) { if (on) b ^= bit; }
            else b = on ? uint8_t(b | bit) : uint8_t(b & ~bit);
        }
    }
    void draw_char(unsigned col, unsigned row, uint8_t ch, uint8_t attr, uint8_t page) {
        if (!ega.graphics) {
            const uint32_t a = 0xB8000 + page * bda16(0x4C) + (row * cols() + col) * 2;
            mem[a] = ch; mem[a + 1] = attr;
            return;
        }
        const uint32_t font = L(m16(0x43 * 4 + 2), m16(0x43 * 4));
        const unsigned h = char_h();
        for (unsigned y = 0; y < h; ++y) {
            const uint8_t bits = mem[font + ch * h + y];
            for (unsigned x = 0; x < 8; ++x) {
                const bool set = bits & (0x80 >> x);
                if (attr & 0x80) { if (set) put_pixel(col * 8 + x, row * h + y, attr & 0x0F, true); }
                else put_pixel(col * 8 + x, row * h + y, set ? (attr & 0x0F) : 0, false);
            }
        }
    }
    void scroll(bool up, unsigned lines, unsigned r0, unsigned c0, unsigned r1, unsigned c1, uint8_t fill, uint8_t page) {
        if (r1 >= rows()) r1 = rows() - 1;
        if (c1 >= cols()) c1 = cols() - 1;
        if (r0 > r1 || c0 > c1) return;
        const unsigned height = r1 - r0 + 1;
        if (lines == 0 || lines > height) lines = height;
        auto copy_row = [&](unsigned dst, unsigned src) {
            if (!ega.graphics) {
                const uint32_t base = 0xB8000 + page * bda16(0x4C);
                for (unsigned c = c0; c <= c1; ++c) {
                    mem[base + (dst * cols() + c) * 2] = mem[base + (src * cols() + c) * 2];
                    mem[base + (dst * cols() + c) * 2 + 1] = mem[base + (src * cols() + c) * 2 + 1];
                }
            } else {
                for (unsigned y = 0; y < char_h(); ++y)
                    for (int p = 0; p < 4; ++p)
                        std::memmove(&ega.planes[p][(dst * char_h() + y) * 80 + c0], &ega.planes[p][(src * char_h() + y) * 80 + c0], c1 - c0 + 1);
            }
        };
        auto clear_row = [&](unsigned r) {
            if (!ega.graphics) {
                const uint32_t base = 0xB8000 + page * bda16(0x4C);
                for (unsigned c = c0; c <= c1; ++c) { mem[base + (r * cols() + c) * 2] = ' '; mem[base + (r * cols() + c) * 2 + 1] = fill; }
            } else {
                for (unsigned y = 0; y < char_h(); ++y)
                    for (int p = 0; p < 4; ++p)
                        std::memset(&ega.planes[p][(r * char_h() + y) * 80 + c0], (fill >> p) & 1 ? 0xFF : 0x00, c1 - c0 + 1);
            }
        };
        if (up) {
            for (unsigned r = r0; r + lines <= r1; ++r) copy_row(r, r + lines);
            for (unsigned r = r1 + 1 - lines; r <= r1; ++r) clear_row(r);
        } else {
            for (unsigned r = r1; r >= r0 + lines; --r) copy_row(r, r - lines);
            for (unsigned r = r0; r < r0 + lines; ++r) clear_row(r);
        }
    }
    void set_font_vector_for_mode(int m) {
        uint16_t off = 0x01E9;  // 8x8 (first half) in the reference video ROM
        if (m == 0x0F || m == 0x10) off = 0x09E9;            // 8x14
        else if (m == 0x11 || m == 0x12 || m == 0x13) off = 0x1916;  // 8x16
        if (m != 3 && m != 2 && m != 7) { s16(0x43 * 4, off); s16(0x43 * 4 + 2, 0xC000); }
    }
    void int16();
    void int1a();
    void int21();
    void service(uint8_t vector);
    void int08();

    // ---------------- DOS helpers ----------------
    std::string read_asciiz(uint16_t s, uint16_t o) {
        std::string r;
        for (int i = 0; i < 128; ++i) { char ch = char(mem[L(s, uint16_t(o + i))]); if (!ch) break; r += ch; }
        return r;
    }
    std::string host_path(const std::string& dos) {
        std::string p = dos;
        if (p.size() > 1 && p[1] == ':') p = p.substr(2);
        for (auto& ch : p) if (ch == '\\') ch = '/';
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        // case-insensitive lookup of each component inside data_dir
        std::string cur = opt.data_dir;
        size_t pos = 0;
        while (pos <= p.size()) {
            size_t n = p.find('/', pos);
            std::string part = p.substr(pos, n == std::string::npos ? std::string::npos : n - pos);
            pos = n == std::string::npos ? p.size() + 1 : n + 1;
            if (part.empty()) continue;
            std::string match = part;
            if (DIR* d = opendir(cur.c_str())) {
                while (dirent* e = readdir(d)) {
                    if (strcasecmp(e->d_name, part.c_str()) == 0) { match = e->d_name; break; }
                }
                closedir(d);
            }
            cur += "/" + match;
        }
        return cur;
    }
    int alloc_handle() { for (int i = 5; i < 20; ++i) if (!files[i].f && !files[i].device) return i; return -1; }
    void dos_error(uint16_t code) { c.ax = code; setf(c, F_CF, true); }
    void dos_ok() { setf(c, F_CF, false); }
    // MCB memory management (first fit, merging free neighbours like DOS)
    uint16_t first_mcb() const { return kBootEnv.first_mcb; }
    void mcb_merge(uint16_t seg) {
        while (mem[L(seg, 0)] == 'M') {
            uint16_t next = uint16_t(seg + m16(L(seg, 3)) + 1);
            if (m16(L(next, 1)) != 0 || m16(L(seg, 1)) != 0) break;
            mem[L(seg, 0)] = mem[L(next, 0)];
            s16(L(seg, 3), uint16_t(m16(L(seg, 3)) + m16(L(next, 3)) + 1));
        }
    }
    void dos_alloc(uint16_t paras) {
        uint16_t seg = first_mcb(), best = 0;
        for (;;) {
            if (m16(L(seg, 1)) == 0) mcb_merge(seg);
            const uint16_t size = m16(L(seg, 3));
            if (m16(L(seg, 1)) == 0) {
                if (size >= paras) {
                    if (size > paras) {  // split
                        const uint16_t rest = uint16_t(seg + paras + 1);
                        mem[L(rest, 0)] = mem[L(seg, 0)];
                        s16(L(rest, 1), 0);
                        s16(L(rest, 3), uint16_t(size - paras - 1));
                        mem[L(seg, 0)] = 'M';
                        s16(L(seg, 3), paras);
                    }
                    s16(L(seg, 1), m16(L(kBootEnv.psp_seg, 0)) ? current_psp() : current_psp());
                    c.ax = uint16_t(seg + 1);
                    dos_ok();
                    return;
                }
                if (size > best) best = size;
            }
            if (mem[L(seg, 0)] == 'Z') break;
            seg = uint16_t(seg + size + 1);
        }
        c.bx = best;
        dos_error(8);
    }
    uint16_t psp_seg_ = 0;
    uint16_t current_psp() const { return psp_seg_; }
    void dos_resize(uint16_t block, uint16_t paras) {
        const uint16_t mcb = uint16_t(block - 1);
        if (mem[L(mcb, 0)] != 'M' && mem[L(mcb, 0)] != 'Z') { dos_error(9); return; }
        const uint16_t size = m16(L(mcb, 3));
        if (paras <= size) {
            if (paras < size) {
                const uint16_t rest = uint16_t(block + paras);
                mem[L(rest, 0)] = mem[L(mcb, 0)];
                s16(L(rest, 1), 0);
                s16(L(rest, 3), uint16_t(size - paras - 1));
                mem[L(mcb, 0)] = 'M';
                s16(L(mcb, 3), paras);
                mcb_merge(rest);
            }
            dos_ok();
            return;
        }
        // grow: absorb the following free block if large enough
        if (mem[L(mcb, 0)] == 'M') {
            const uint16_t next = uint16_t(block + size);
            if (m16(L(next, 1)) == 0) {
                mcb_merge(next);
                const uint16_t total = uint16_t(size + 1 + m16(L(next, 3)));
                if (total >= paras) {
                    mem[L(mcb, 0)] = mem[L(next, 0)];
                    s16(L(mcb, 3), total);
                    dos_resize(block, paras);
                    return;
                }
                c.bx = total;
                dos_error(8);
                return;
            }
        }
        c.bx = size;
        dos_error(8);
    }
};

// ---------------- global hooks used by recompiled code ----------------
static Machine* g_m = nullptr;
static uint8_t vram_read(uint32_t a) { return g_m->ega.vram_read(a, g_m->mem.data()); }
static void vram_write(uint32_t a, uint8_t v) { g_m->ega.vram_write(a, v, g_m->mem.data()); }
MemRead g_vram_read = vram_read;
MemWrite g_vram_write = vram_write;
uint8_t* g_code_map = nullptr;
void rt_code_write(uint32_t a, uint8_t v) {
    Machine* m = g_m;
    // Known, handled pattern: the 8087 emulator turns INT 3Dh (CD 3D) into 89 C0; the translated INT 3Dh
    // checks the byte in memory. Every other change to translated code is unhandled.
    const bool int3d_patch = (m->mem[a] == 0xCD && v == 0x89 && m->mem[a + 1] == 0x3D) ||
                             (m->mem[a] == 0x3D && v == 0xC0 && m->mem[a - 1] == 0x89);
    if (!int3d_patch) m->smc_unhandled.insert(a);
    if (m->smc_seen.insert(a).second && !int3d_patch) {
        std::fprintf(stderr, "[rc] self-modifying write %05X: %02X -> %02X (block %04X:%04X, t=%.1fms)\n", a,
                     m->mem[a], v, m->cur_block >> 16, m->cur_block & 0xFFFF, double(m->c.icount) / m->ipms());
    }
}

void rt_raise(Cpu& c, uint8_t n) { (void)c; g_m->raise(n); }
// I/O bus delay as the reference charges it (DOSBox-X iohandler.cpp, iodelay=-1, ISA clock 25 MHz / 3):
// 8-bit transfer 8.5 bus clocks (1020 ns), 16-bit 5.5 (660 ns); a write costs 3/4 of a read; converted to
// CPU cycles at the configured speed and truncated. Not charged inside callbacks (native services).
static uint64_t io_cycles(double ns, bool write) {
    const double c = g_m->ipms() * ns / 1e6;
    return uint64_t(write ? c * 3 / 4 : c);
}
uint8_t rt_in8(Cpu& c, uint16_t port) { c.icount += io_cycles(1020, false); return g_m->in8(port); }
uint16_t rt_in16(Cpu& c, uint16_t port) {
    c.icount += io_cycles(660, false);
    return uint16_t(g_m->in8(port) | (g_m->in8(uint16_t(port + 1)) << 8));
}
void rt_out8(Cpu& c, uint16_t port, uint8_t v) {
    c.icount += io_cycles(1020, true);
    if (g_m->io_log_on) g_m->io_log.push_back(c.icount << 32 | uint64_t(port) << 16 | v);
    g_m->out8(port, v);
}
void rt_out16(Cpu& c, uint16_t port, uint16_t v) {
    c.icount += io_cycles(660, true);
    if (g_m->io_log_on) g_m->io_log.push_back(c.icount << 32 | uint64_t(port) << 16 | v);
    g_m->out8(port, uint8_t(v));
    g_m->out8(uint16_t(port + 1), uint8_t(v >> 8));
}
void rt_halt(Cpu& c) { c.icount += 100; }
void rt_unsupported(Cpu& c, const char* what) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "unsupported instruction at %04X:%04X: %s", c.cs, c.ip, what);
    g_m->message = buf;
    g_m->state = RunState::Unsupported;
    throw RunState::Unsupported;
}

// ---------------- BIOS services ----------------
static const char kScanAscii[0x3A] = {0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k',
    'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '};
static const char kScanShift[0x3A] = {0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K',
    'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '};

// IRQ0 callback (reference stub: sti, callback, push ds/ax/dx, INT 1Ch, EOI, iret): DOSBox-X INT8_Handler.
void Machine::int08() {
    uint32_t t = *reinterpret_cast<uint32_t*>(&mem[0x46C]) + 1;
    if (t >= 0x1800B0) { ++mem[0x470]; t = 0; }  // midnight
    std::memcpy(&mem[0x46C], &t, 4);
    const uint8_t motor = mem[0x440];             // diskette motor timeout (stops at zero on EGA/VGA PCs)
    if (motor) mem[0x440] = uint8_t(motor - 1);
    if (motor == 1) mem[0x43F] &= 0xF0;
}

// IRQ1 callback (reference stub: push ax, in al,60h, INT 15h AH=4Fh, callback if CF, EOI, pop ax, iret).
void Machine::int09() {
    const uint8_t sc = c.al;
    uint8_t& flags = mem[0x417];
    const bool release = sc & 0x80;
    const uint8_t code = sc & 0x7F;
    if (sc == 0xE0) { e0_prefix = true; }
    else {
        if (code == 0x2A) flags = release ? flags & ~0x02 : flags | 0x02;        // left shift
        else if (code == 0x36) flags = release ? flags & ~0x01 : flags | 0x01;   // right shift
        else if (code == 0x1D) flags = release ? flags & ~0x04 : flags | 0x04;   // ctrl
        else if (code == 0x38) flags = release ? flags & ~0x08 : flags | 0x08;   // alt
        else if (!release) {
            const bool shift = flags & 0x03;
            uint8_t ascii = 0;
            if (code < 0x3A && !e0_prefix) ascii = uint8_t(shift ? kScanShift[code] : kScanAscii[code]);
            if (flags & 0x04 && ascii >= 'a' && ascii <= 'z') ascii = uint8_t(ascii - 'a' + 1);
            if (flags & 0x04 && ascii >= 'A' && ascii <= 'Z') ascii = uint8_t(ascii - 'A' + 1);
            if (flags & 0x08) ascii = 0;
            kbd_put(uint16_t((code << 8) | ascii));
        }
        e0_prefix = false;
    }
}

void Machine::int16() {
    uint16_t w;
    switch (c.ah) {
        case 0x00: case 0x10:
            if (!kbd_peek(w)) {        // wait: step over the IRET into the stub's NOP loop back to the callback
                c.ip += 1;
                return;
            }
            kbd_pop();
            c.ax = w;
            break;
        case 0x01: case 0x11:
            if (kbd_peek(w)) { c.ax = w; setf(c, F_ZF, false); } else setf(c, F_ZF, true);
            break;
        case 0x02: case 0x12: c.al = mem[0x417]; break;
        default: break;
    }
    cb_flags(F_ZF);
}

void Machine::int1a() {
    switch (c.ah) {
        case 0x00: {
            uint32_t t = *reinterpret_cast<uint32_t*>(&mem[0x46C]);
            if (tick_override_pos < opt.tick_overrides.size()) t = opt.tick_overrides[tick_override_pos++];
            c.cx = uint16_t(t >> 16);
            c.dx = uint16_t(t);
            c.al = mem[0x470];
            mem[0x470] = 0;
            break;
        }
        case 0x01: s16(0x46C, c.dx); s16(0x46E, c.cx); break;
        case 0x02: {  // RTC time from the tick count (BCD)
            const uint32_t t = *reinterpret_cast<uint32_t*>(&mem[0x46C]);
            const unsigned secs = unsigned(double(t) / 18.2065);
            auto bcd = [](unsigned v) { return uint8_t(((v / 10) << 4) | (v % 10)); };
            c.ch = bcd(secs / 3600); c.cl = bcd(secs / 60 % 60); c.dh = bcd(secs % 60); c.dl = 0;
            setf(c, F_CF, false);
            break;
        }
        case 0x04: c.cx = 0x2026; c.dx = 0x1018; setf(c, F_CF, false); break;
        default: break;
    }
    cb_flags(F_CF);
}

void Machine::int10() {
    switch (c.ah) {
        case 0x00: {
            const int m = c.al & 0x7F;
            ega.set_mode(m, !(c.al & 0x80));
            mem[0x449] = uint8_t(m);
            bda16(0x4A) = 80;
            bda16(0x4C) = m == 0x10 ? 0x8000 : (m == 0x12 ? 0xA000 : 0x1000);
            bda16(0x4E) = 0;
            for (int i = 0; i < 16; ++i) mem[0x450 + i] = 0;
            mem[0x462] = 0;
            mem[0x484] = m == 0x12 ? 29 : 24;
            bda16(0x85) = m == 0x10 ? 14 : 16;
            if (!ega.graphics && !(c.al & 0x80))
                for (uint32_t a = 0xB8000; a < 0xBC000; a += 2) { mem[a] = ' '; mem[a + 1] = 7; }
            set_font_vector_for_mode(m);
            break;
        }
        case 0x06: case 0x07:
            scroll(c.ah == 0x06, c.al, c.ch, c.cl, c.dh, c.dl, c.bh, mem[0x462]);
            break;
        case 0x08: {
            const uint8_t page = c.bh & 7;
            const unsigned col = mem[0x450 + page * 2], row = mem[0x451 + page * 2];
            if (!ega.graphics) { const uint32_t a = 0xB8000 + page * bda16(0x4C) + (row * cols() + col) * 2; c.al = mem[a]; c.ah = mem[a + 1]; }
            else c.ax = 0;
            break;
        }
        case 0x09: case 0x0A: {
            const uint8_t page = c.bh & 7;
            unsigned col = mem[0x450 + page * 2];
            const unsigned row = mem[0x451 + page * 2];
            for (unsigned i = 0; i < c.cx && col < cols(); ++i, ++col) {
                uint8_t attr = c.bl;
                if (c.ah == 0x0A && !ega.graphics) attr = mem[0xB8000 + page * bda16(0x4C) + (row * cols() + col) * 2 + 1];
                draw_char(col, row, c.al, attr, page);
            }
            break;
        }
        case 0x0E: {
            const uint8_t page = mem[0x462];
            unsigned col = mem[0x450 + page * 2], row = mem[0x451 + page * 2];
            switch (c.al) {
                case 7: break;
                case 8: if (col) --col; break;
                case 10: ++row; break;
                case 13: col = 0; break;
                default:
                    draw_char(col, row, c.al, ega.graphics ? c.bl : mem[0xB8000 + page * bda16(0x4C) + (row * cols() + col) * 2 + 1], page);
                    if (++col >= cols()) { col = 0; ++row; }
                    break;
            }
            if (row >= rows()) {
                row = rows() - 1;
                uint8_t fill = 0;
                if (!ega.graphics) fill = mem[0xB8000 + page * bda16(0x4C) + (row * cols() + col) * 2 + 1];
                scroll(true, 1, 0, 0, rows() - 1, cols() - 1, fill, page);
            }
            mem[0x450 + page * 2] = uint8_t(col);
            mem[0x451 + page * 2] = uint8_t(row);
            break;
        }
        case 0x01: bda16(0x60) = c.cx; break;
        case 0x02: mem[0x450 + (c.bh & 7) * 2] = c.dl; mem[0x451 + (c.bh & 7) * 2] = c.dh; break;
        case 0x03: c.dl = mem[0x450 + (c.bh & 7) * 2]; c.dh = mem[0x451 + (c.bh & 7) * 2]; c.cx = bda16(0x60); break;
        case 0x05: mem[0x462] = c.al; break;
        case 0x0F: c.al = mem[0x449]; c.ah = uint8_t(bda16(0x4A)); c.bh = mem[0x462]; break;
        case 0x10:
            switch (c.al) {
                case 0x00: if (c.bl < 16) ega.attr[c.bl] = c.bh; break;
                case 0x01: ega.attr[0x11] = c.bh; break;
                case 0x02: for (int i = 0; i < 16; ++i) ega.attr[i] = mem[L(c.es, uint16_t(c.dx + i))];
                           ega.attr[0x11] = mem[L(c.es, uint16_t(c.dx + 16))]; break;
                case 0x03: ega.attr[0x10] = uint8_t((ega.attr[0x10] & ~0x08) | (c.bl ? 0x08 : 0)); break;
                case 0x07: c.bh = ega.attr[c.bl & 0x1F]; break;
                case 0x08: c.bh = ega.attr[0x11]; break;
                case 0x10: ega.dac[c.bl][0] = c.dh & 0x3F; ega.dac[c.bl][1] = c.ch & 0x3F; ega.dac[c.bl][2] = c.cl & 0x3F; break;
                case 0x12: for (unsigned i = 0; i < c.cx; ++i) for (int k = 0; k < 3; ++k)
                               ega.dac[(c.bx + i) & 0xFF][k] = mem[L(c.es, uint16_t(c.dx + i * 3 + k))] & 0x3F; break;
                case 0x15: c.dh = ega.dac[c.bl][0]; c.ch = ega.dac[c.bl][1]; c.cl = ega.dac[c.bl][2]; break;
                default: std::fprintf(stderr, "[rc] INT 10h AX=%04X not implemented\n", c.ax); break;
            }
            break;
        case 0x11:
            if (c.al == 0x30) {
                static const uint16_t font_off[8] = {0, 0, 0x09E9, 0x01E9, 0x05E9, 0x17E9, 0x1916, 0x2516};
                if (c.bh == 0) { c.bp = m16(0x1F * 4); c.es = m16(0x1F * 4 + 2); }
                else if (c.bh == 1) { c.bp = m16(0x43 * 4); c.es = m16(0x43 * 4 + 2); }
                else if (c.bh < 8) { c.bp = font_off[c.bh]; c.es = 0xC000; }
                c.cx = bda16(0x85);
                c.dl = mem[0x484];
            } else if (c.al == 0x22 || c.al == 0x23 || c.al == 0x24) {
                // ROM graphics fonts (DOSBox-X int10.cpp graphics_chars): INT 43h, char height, rows by BL
                const uint16_t off = c.al == 0x22 ? 0x09E9 : c.al == 0x23 ? 0x01E9 : 0x1916;
                s16(0x43 * 4, off); s16(0x43 * 4 + 2, 0xC000);
                bda16(0x85) = c.al == 0x22 ? 14 : c.al == 0x23 ? 8 : 16;
                mem[0x484] = c.bl == 0 ? uint8_t(c.dl - 1) : c.bl == 1 ? 13 : c.bl == 3 ? 42 : 24;
            } else std::fprintf(stderr, "[rc] INT 10h AX=%04X not implemented\n", c.ax);
            break;
        case 0x12:
            if (c.bl == 0x10) { c.bh = 0; c.bl = 3; c.ch = 0; c.cl = mem[0x488] & 0x0F; }
            break;
        case 0x1A: if (c.al < 2) { c.ax = 0x001A; if (c.al == 0) c.bx = 0x0008; } break;
        case 0x1B:
            if (c.bx == 0) {
                const uint32_t sv = L(c.es, c.di);
                s16(sv, 0x2A5A); s16(sv + 2, 0xC000);                 // static functionality table
                for (int i = 0; i < 0x1E; ++i) mem[sv + 4 + i] = mem[0x449 + i];
                mem[sv + 0x22] = uint8_t(mem[0x484] + 1);
                mem[sv + 0x23] = mem[0x485]; mem[sv + 0x24] = mem[0x486];
                for (int i = 0x25; i < 0x40; ++i) mem[sv + i] = 0;
                s16(sv + 0x25, 0x0008);                               // display combination: VGA colour
                const int m = mem[0x449];
                s16(sv + 0x27, (m == 0x0F || m == 0x11) ? 2 : (m == 0x13 ? 256 : 16));
                mem[sv + 0x29] = uint8_t(m == 3 ? 8 : (m == 0x10 ? 2 : (m == 0x0D ? 8 : (m == 0x0E ? 4 : 1))));
                mem[sv + 0x2A] = uint8_t(m == 0x10 || m == 0x0F ? 1 : (m == 0x11 || m == 0x12 ? 3 : (m == 3 ? 2 : 0)));
                mem[sv + 0x2D] = ega.graphics ? 0x01 : 0x21;
                mem[sv + 0x31] = 3;
                c.al = 0x1B;
            } else c.al = 0;
            break;
        case 0xEF: break;  // Hercules/MDA probe: not present (DOSBox-X leaves registers unchanged)
        default:
            std::fprintf(stderr, "[rc] INT 10h AH=%02X not implemented (mode %02X)\n", c.ah, mem[0x449]);
            break;
    }
}

void Machine::int21() {
    switch (c.ah) {
        case 0x00: case 0x4C:
            message = "program exited with code " + std::to_string(c.ah == 0x4C ? c.al : 0);
            state = RunState::Exited;
            return;
        case 0x02: std::fputc(c.dl, stderr); break;
        case 0x06: if (c.dl == 0xFF) { uint16_t w; if (kbd_peek(w)) { kbd_pop(); c.al = uint8_t(w); setf(c, F_ZF, false); } else { c.al = 0; setf(c, F_ZF, true); } } break;
        case 0x07: case 0x08: case 0x01: {
            uint16_t w;
            if (!kbd_peek(w)) { c.ip = uint16_t(c.ip - 4); return; }  // wait: run the callback again
            kbd_pop(); c.al = uint8_t(w); break;
        }
        case 0x09: { for (uint16_t o = c.dx; mem[L(c.ds, o)] != '$'; ++o) std::fputc(mem[L(c.ds, o)], stderr); break; }
        case 0x0B: { uint16_t w; c.al = kbd_peek(w) ? 0xFF : 0x00; c.ip += 2; break; }  // + DOS overhead loop
        case 0x0C: { bda16(0x1A) = bda16(0x1C); uint8_t f = c.al; if (f == 1 || f == 6 || f == 7 || f == 8) { c.ah = f; int21(); return; } break; }
        case 0x0D: break;
        case 0x0E: c.al = 26; break;
        case 0x19: c.al = 2; break;
        case 0x1A: dta_seg = c.ds; dta_off = c.dx; break;
        case 0x25: s16(c.al * 4u, c.dx); s16(c.al * 4u + 2, c.ds); break;
        case 0x2A: c.cx = 2026; c.dh = 10; c.dl = 18; c.al = 0; break;
        case 0x2C: {
            // As the reference DOS (DOSBox-X dos.cpp): hundredths since midnight from the BIOS tick count in
            // one double expression, then split. TIMER (and so RANDOMIZE TIMER) depends on this rounding.
            // DOSBox-X reads the BIOS clock through INT 1Ah AH=0 as real code (real_int), keeps AL (midnight
            // flag) and sets AH=2Ch.
            c.ax = 0;
            real_int(0x1A);
            c.ah = 0x2C;
            if (dos_time_pos < opt.dos_time_replay.size()) {
                const auto& r = opt.dos_time_replay[dos_time_pos++];
                const uint32_t caller = (uint32_t(m16(L(c.ss, uint16_t(c.sp + 2)))) << 16) | m16(L(c.ss, c.sp));
                if (caller != r.caller) {
                    char buf[120];
                    std::snprintf(buf, sizeof buf, "clock replay diverged at reading %zu: caller %04X:%04X, recorded %04X:%04X",
                                  dos_time_pos, caller >> 16, caller & 0xFFFF, r.caller >> 16, r.caller & 0xFFFF);
                    message = buf;
                    state = RunState::Unsupported;
                    throw state;
                }
                c.cx = r.cx; c.dx = r.dx;
                c.ip += 2;  // same DOS overhead as a live reading
                break;
            }
            const uint32_t t = (uint32_t(c.cx) << 16) | c.dx;
            uint64_t h = uint64_t((100.0 / (1193182.0 / 65536.0)) * double(t));
            c.dl = uint8_t(h % 100); h /= 100;
            c.dh = uint8_t(h % 60); h /= 60;
            c.cl = uint8_t(h % 60); h /= 60;
            c.ch = uint8_t(h % 24);
            c.ip += 2;  // DOSBox-X overhead(): continue in the stub's delay loop (push cx; mov cx,140h; loop $)
            break;
        }
        case 0x2F: c.es = dta_seg; c.bx = dta_off; break;
        case 0x30: c.al = 5; c.ah = 0; c.bh = 0xFF; c.bl = 0; c.cx = 0; break;
        case 0x33: if (c.al == 0) c.dl = 0; else if (c.al == 6) { c.bx = 0x0005; c.dx = 0; } break;
        case 0x35: c.bx = m16(c.al * 4u); c.es = m16(c.al * 4u + 2); break;
        case 0x36: c.ax = 64; c.bx = 0x7FFF; c.cx = 512; c.dx = 0xFFFF; break;
        case 0x37: if (c.al == 0) c.dl = '/'; break;
        case 0x3C: case 0x3D: {
            const uint8_t fn = c.ah;
            pic_imr &= uint8_t(~1);  // DOSBox-X disk I/O unmasks the system timer (disk_io_unmask_irq0)
            const std::string dos = read_asciiz(c.ds, c.dx);
            const std::string path = host_path(dos);
            const int h = alloc_handle();
            if (h < 0) { dos_error(4); break; }
            const char* mode = c.ah == 0x3C ? "w+b" : ((c.al & 3) == 0 ? "rb" : "r+b");
            FILE* f = std::fopen(path.c_str(), mode);
            if (!f) { dos_error(c.ah == 0x3C ? 3 : 2); break; }
            files[h].f = f; files[h].path = path;
            c.ax = uint16_t(h);
            diskio_delay(fn == 0x3C ? 2048 : 1024);
            dos_ok();
            break;
        }
        case 0x3E:
            pic_imr &= uint8_t(~1);
            // AL: the file's reference count before the close (DOSBox-X DOS_CloseFile), 1 for an open file.
            if (c.bx < 20 && files[c.bx].f) { std::fclose(files[c.bx].f); files[c.bx].f = nullptr; c.al = 1; diskio_delay(512); dos_ok(); }
            else if (c.bx < 5) dos_ok();
            else dos_error(6);
            break;
        case 0x3F: {
            if (c.bx < 5) { c.ax = 0; dos_ok(); break; }
            if (c.bx >= 20 || !files[c.bx].f) { dos_error(6); break; }
            std::vector<uint8_t> buf(c.cx);
            const size_t n = std::fread(buf.data(), 1, buf.size(), files[c.bx].f);
            for (size_t i = 0; i < n; ++i) wr8(c, c.ds, uint16_t(c.dx + i), buf[i]);
            c.ax = uint16_t(n);
            diskio_delay(double(n));
            dos_ok();
            break;
        }
        case 0x40: {
            if (c.bx >= 1 && c.bx <= 4) {
                for (unsigned i = 0; i < c.cx; ++i) std::fputc(mem[L(c.ds, uint16_t(c.dx + i))], stderr);
                c.ax = c.cx; dos_ok(); break;
            }
            if (c.bx >= 20 || !files[c.bx].f) { dos_error(6); break; }
            std::vector<uint8_t> buf(c.cx);
            for (unsigned i = 0; i < c.cx; ++i) buf[i] = rd8(c, c.ds, uint16_t(c.dx + i));
            const size_t n = std::fwrite(buf.data(), 1, buf.size(), files[c.bx].f);
            std::fflush(files[c.bx].f);
            c.ax = uint16_t(n);
            diskio_delay(double(n));
            dos_ok();
            break;
        }
        case 0x41: {
            pic_imr &= uint8_t(~1);
            const std::string path = host_path(read_asciiz(c.ds, c.dx));
            if (std::remove(path.c_str()) == 0) { diskio_delay(1024); dos_ok(); } else dos_error(2);
            break;
        }
        case 0x42: {
            if (c.bx >= 20 || !files[c.bx].f) { dos_error(6); break; }
            const long off = long((uint32_t(c.cx) << 16) | c.dx);
            const int whence = c.al == 0 ? SEEK_SET : (c.al == 1 ? SEEK_CUR : SEEK_END);
            std::fseek(files[c.bx].f, c.al == 0 ? long(uint32_t(off)) : off, whence);
            const long pos = std::ftell(files[c.bx].f);
            c.ax = uint16_t(pos); c.dx = uint16_t(uint32_t(pos) >> 16);
            diskio_delay(32);
            dos_ok();
            break;
        }
        case 0x43: {
            const std::string path = host_path(read_asciiz(c.ds, c.dx));
            struct stat st;
            if (stat(path.c_str(), &st) != 0) { dos_error(2); break; }
            if (c.al == 0) c.cx = 0x20;
            dos_ok();
            break;
        }
        case 0x44:
            if (c.al == 0x00) {
                // DOSBox-X DOS_IOCTL reads the device information twice (device test, then the value), each
                // time running INT 16h; the result is the device word with the character-device bit.
                // Handles 0-4 of the reference all refer to its CON device (its JFT maps them to SFT entries
                // 1,1,1,0,2; the reference's instruction trace shows CON's INT 16h check for each).
                if (c.bx < 5) { (void)con_information(); c.dx = con_information() | 0x8000; }
                else if (c.bx < 20 && files[c.bx].f) c.dx = 0x0002;
                else { dos_error(6); break; }
                c.ax = c.dx;
                dos_ok();
            } else if (c.al == 0x01) { if (c.bx < 5) con_binary = c.dx & 0x20; dos_ok(); }
            else if (c.al == 0x08) { c.ax = 1; dos_ok(); }
            else { std::fprintf(stderr, "[rc] INT 21h AX=%04X not implemented\n", c.ax); dos_error(1); }
            break;
        case 0x45: {
            const int h = alloc_handle();
            if (h < 0 || c.bx >= 20) { dos_error(4); break; }
            if (c.bx < 5) { files[h].device = true; }
            else if (files[c.bx].f) { files[h].f = std::fopen(files[c.bx].path.c_str(), "r+b"); files[h].path = files[c.bx].path; }
            c.ax = uint16_t(h);
            dos_ok();
            break;
        }
        case 0x47: mem[L(c.ds, c.si)] = 0; dos_ok(); break;
        case 0x48: dos_alloc(c.bx); break;
        case 0x49: {
            const uint16_t mcb = uint16_t(c.es - 1);
            if (mem[L(mcb, 0)] != 'M' && mem[L(mcb, 0)] != 'Z') { dos_error(9); break; }
            s16(L(mcb, 1), 0);
            dos_ok();
            break;
        }
        case 0x4A: dos_resize(c.es, c.bx); break;
        case 0x4E: dos_error(18); break;  // find first: no matches (wildcards not used by the game)
        case 0x4F: dos_error(18); break;
        case 0x51: case 0x62: c.bx = current_psp(); break;
        case 0x54: c.al = 0; break;
        case 0x58: if (c.al == 0) c.ax = 0; dos_ok(); break;
        case 0x59: c.ax = 0; c.bx = 0; c.cx = 0; break;
        default:
            std::fprintf(stderr, "[rc] INT 21h AH=%02X not implemented\n", c.ah);
            dos_error(1);
            break;
    }
    cb_flags();
}

void Machine::service(uint8_t n) {
    switch (n) {
        case 0x08: int08(); return;
        case 0x09: int09(); return;
        case 0x10: int10(); return;
        case 0x11: c.ax = bda16(0x10); return;
        case 0x12: c.ax = bda16(0x13); return;
        case 0x15:  // AH=4Fh keyboard intercept: carry set, key processed; others: unsupported (AH=86h)
            if (c.ah != 0x4F) c.ah = 0x86;
            setf(c, F_CF, true);
            cb_flags(F_CF);
            return;
        case 0x16: int16(); return;
        case 0x1A: int1a(); return;
        case 0x21: int21(); return;
        case 0x2F: if (c.ax == 0x4300) c.al = 0; else if (c.ax == 0x1600) c.al = 0; return;
        default: return;  // the reference's default handler (shared stub): no effect
    }
}

void rt_callback(Cpu& c, uint16_t number) {
    Machine& m = *g_m;
    static std::vector<int16_t> vector_of;
    if (vector_of.empty()) {
        vector_of.assign(65536, -1);
        for (unsigned i = 0; i < kBootEnv.callback_count; ++i) vector_of[kBootEnv.callbacks[i].callback] = kBootEnv.callbacks[i].vector;
    }
    static FILE* cb_log = std::getenv("RC_TRACE_CB") ? std::fopen(std::getenv("RC_TRACE_CB"), "w") : nullptr;
    if (cb_log) std::fprintf(cb_log, "%u %04X %.6f\n", number, c.ax, double(c.icount) / m.ipms());  // as DOSBox-X REA_LOG_CB
    if (number == kBootEnv.realint_stop || number == kBootEnv.idle_stop) { ++m.realint_stops; return; }  // run_nested end
    const int v = vector_of[number];
    if (m.opt.trace_ints)
        std::fprintf(stderr, "[rc] INT %02X AX=%04X BX=%04X CX=%04X DX=%04X from %04X:%04X t=%.1fms\n", v & 0xFF, c.ax, c.bx, c.cx,
                     c.dx, m.m16(m.L(c.ss, uint16_t(c.sp + 2))), m.m16(m.L(c.ss, c.sp)), double(c.icount) / m.ipms());
    if (v < 0) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "reference callback %u at %04X:%04X serves no known vector", number, c.cs, uint16_t(c.ip - 4));
        m.message = buf;
        m.state = RunState::Unsupported;
        throw RunState::Unsupported;
    }
    m.service(uint8_t(v));
}

// ---------------- API ----------------
Machine* machine_create() { return new Machine(); }
void machine_destroy(Machine* m) {
    if (g_m == m) g_m = nullptr;
    if (m->speaker_io) std::fclose(m->speaker_io);
    if (m->missing) std::fclose(m->missing);
    delete m;
}

bool machine_init(Machine* m, const MachineOptions& o, std::string& error) {
    g_m = m;
    m->opt = o;
    m->pokes = o.pokes;
    for (const NativeProc& p : native_registry())
        if (p.verified || o.native == MachineOptions::Native::Verify) m->natives[(uint32_t(p.cs) << 16) | p.ip] = p;
    m->vga = o.vga_timing;
    if (m->vga.empty()) m->vga.assign(kBootEnv.vga, kBootEnv.vga + kBootEnv.vga_count);
    Cpu& c = m->c;
    c.mem = m->mem.data();
    m->code_map.assign(0x110000 / 8 + 1, 0);
    for (unsigned i = 0; i < g_block_count; ++i) {
        m->blocks[(uint32_t(g_blocks[i].cs) << 16) | g_blocks[i].ip] = g_blocks[i].fn;
        m->block_bytes[(uint32_t(g_blocks[i].cs) << 16) | g_blocks[i].ip] = g_blocks[i].nbytes;
        const uint32_t start = (uint32_t(g_blocks[i].cs) << 4) + g_blocks[i].ip;
        for (uint32_t a = start; a < start + g_blocks[i].nbytes; ++a) m->code_map[a >> 3] |= uint8_t(1 << (a & 7));
    }
    g_code_map = m->code_map.data();
    // The reference machine around the program: IVT, BDA, DOS data, MCBs, environment, PSP, ROMs, HMA.
    for (unsigned i = 0; i < kBootEnv.region_count; ++i)
        std::memcpy(&m->mem[kBootEnv.regions[i].linear], kBootEnv.regions[i].data, kBootEnv.regions[i].size);
    *reinterpret_cast<uint32_t*>(&m->mem[0x46C]) = o.start_ticks;
    m->psp_seg_ = kBootEnv.psp_seg;
    m->dta_seg = kBootEnv.psp_seg;
    // Program image: the generated blocks were translated for g_load_seg; the loader must agree.
    if (uint16_t(kBootEnv.psp_seg + 0x10) != g_load_seg) { error = "boot environment and translation disagree on the load segment"; return false; }
    extern const uint8_t g_image[]; extern const unsigned g_image_size;
    std::memcpy(&m->mem[uint32_t(g_load_seg) << 4], g_image, g_image_size);
    // EGA starts in text mode 3 (as DOSBox at the prompt)
    m->ega.set_mode(3, false);
    const BootRegs& r = kBootEnv.regs;
    c.ax = r.ax; c.bx = r.bx; c.cx = r.cx; c.dx = r.dx; c.si = r.si; c.di = r.di; c.bp = r.bp; c.sp = r.sp;
    c.ds = r.ds; c.es = r.es; c.ss = r.ss; c.cs = r.cs; c.ip = r.ip; set_flags(c, r.flags);
    // The BIOS timer runs from before the entry: channel 0 (count 65536) is pit0_phase ms into its period.
    const double phase = o.pit0_phase_ms >= 0 ? o.pit0_phase_ms : kBootEnv.pit0_phase_ms;
    m->pit[0].start_icount = -int64_t(std::llround(phase * m->ipms()));
    m->next_irq0_icount = double(m->pit[0].start_icount) + 65536 * m->icount_per_pit_tick();
    if (!o.missing_log.empty()) m->missing = std::fopen(o.missing_log.c_str(), "a");
    if (!o.speaker_io_log.empty()) m->speaker_io = std::fopen(o.speaker_io_log.c_str(), "w");
    return true;
}

std::vector<NativeProc>& native_registry() { static std::vector<NativeProc> r; return r; }

void rt_call_far(Cpu& c, uint16_t seg, uint16_t off, uint16_t ret_ip, bool announce) {
    push16(c, c.cs);
    push16(c, ret_ip);
    const uint16_t ret_cs = c.cs, sp_ret = c.sp;
    c.cs = seg;
    c.ip = off;
    g_m->run_until(ret_cs, ret_ip, sp_ret, true);
    if (announce) g_m->native_block(ret_ip);  // the return point starts a block
    else c.ip = ret_ip;
}

void rt_at(Cpu& c, uint16_t ip) { g_m->native_block(ip); }

void rt_run(Cpu& c, uint16_t from_ip, uint16_t to_ip) {
    const uint16_t cs0 = c.cs;
    const bool suspended = g_m->natives_suspended;
    c.ip = from_ip;
    ++g_m->nesting;
    g_m->natives_suspended = true;  // the original code itself, even where a native region starts
    do {
        if (!g_m->step()) { g_m->natives_suspended = suspended; throw g_m->state; }
    } while (!(c.cs == cs0 && c.ip == to_ip));
    g_m->natives_suspended = suspended;
    --g_m->nesting;
    g_m->native_block(to_ip);
}

void rt_int(Cpu& c, uint8_t n, uint16_t next_ip, uint16_t cont_ip) {
    const uint16_t cs0 = c.cs, sp0 = c.sp;
    c.ip = next_ip;
    rt_raise(c, n);
    g_m->run_until(cs0, cont_ip, uint16_t(sp0 - 1));
    g_m->native_block(cont_ip);
}

void rt_call_near(Cpu& c, uint16_t off, uint16_t ret_ip, bool announce) {
    push16(c, ret_ip);
    const uint16_t sp_ret = c.sp;
    c.ip = off;
    g_m->run_until(c.cs, ret_ip, sp_ret, true);
    if (announce) g_m->native_block(ret_ip);
    else c.ip = ret_ip;
}

RunState machine_run_ms(Machine* m, double ms) {
    g_m = m;
    Cpu& c = m->c;
    const double target = double(c.icount) + ms * m->ipms();
    if (m->state == RunState::Break) { m->state = RunState::Running; m->resume_addr = (uint32_t(c.cs) << 16) | c.ip; }
    try {
        while (m->state == RunState::Running && double(c.icount) < target && m->step()) {}
    } catch (RunState) {
    }
    return m->state;
}

RunState machine_state(const Machine* m) { return m->state; }
void machine_stop(Machine* m) { m->state = RunState::Halted; }
std::string machine_native_report(const Machine* m) {
    std::string r;
    for (const auto& [name, n] : m->verified_calls) r += name + "=" + std::to_string(n) + " ";
    if (!m->nested_runs.empty()) {
        r += "| nested:";
        for (const auto& [name, n] : m->nested_runs) r += " " + name + "=" + std::to_string(n);
    }
    return r;
}
std::string machine_message(const Machine* m) { return m->message; }
void machine_key(Machine* m, uint8_t sc) { m->key_queue.push_back(sc); }
void machine_render(const Machine* m, std::vector<uint8_t>& idx, int& w, int& h, uint32_t rgb[16]) {
    m->ega.render_indices(idx, w, h);
    for (int i = 0; i < 16; ++i) rgb[i] = m->ega.rgb_of_index(i);
}
std::vector<std::pair<uint64_t, uint32_t>> machine_take_speaker(Machine* m) {
    std::vector<std::pair<uint64_t, uint32_t>> out;
    out.swap(m->speaker_events);
    return out;
}
uint64_t machine_icount(const Machine* m) { return m->c.icount; }
void machine_profile(Machine* m, bool on) { m->profiling = on; if (on) m->profile.clear(); }
std::vector<std::pair<uint32_t, uint64_t>> machine_profile_top(const Machine* m, size_t n) {
    std::vector<std::pair<uint32_t, uint64_t>> v(m->profile.begin(), m->profile.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (v.size() > n) v.resize(n);
    return v;
}
const uint8_t* machine_memory(const Machine* m) { return m->mem.data(); }
void machine_add_breakpoint(Machine* m, uint16_t cs, uint16_t ip) { m->breakpoints.insert((uint32_t(cs) << 16) | ip); }
uint32_t machine_cs_ip(const Machine* m) { return (uint32_t(m->c.cs) << 16) | m->c.ip; }
uint32_t machine_ss_sp(const Machine* m) { return (uint32_t(m->c.ss) << 16) | m->c.sp; }
uint8_t* machine_memory_rw(Machine* m) { return m->mem.data(); }
void machine_out8(Machine* m, uint16_t port, uint8_t v) { g_m = m; rt_out8(m->c, port, v); }
const uint8_t* machine_plane(const Machine* m, int p) { return m->ega.planes[p & 3]; }

}  // namespace rc
