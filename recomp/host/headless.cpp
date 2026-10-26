// Headless runner for the recompiled game: runs for a given emulated time with scripted keys and writes
// the screen (PNG) and raw state for lockstep comparison.
//   rc_headless --data DIR --ms N [--keys "t_ms:scancode,..."] [--png out.png] [--planes prefix] [--missing file]
// Lockstep mode (mirrors tools/lockstep.py golden captures in DOSBox-X):
//   --bda-keys "3100,..."  key words placed in the BIOS keyboard buffer at program start
//   --speaker-io FILE  log PIT channel 2 / port 61h writes ("PP VV ms")
//   --dos-time FILE  replay recorded DOS get-time results (clock input), in order
//   --native  run the decompiled procedures (src/game); --verify-native  check each call against the
//             recompiled procedure (stops at the first difference)
//   --pit0-phase MS  BIOS timer tick phase at the entry of the golden run (refmachine.pit0_phase)
//   --vga-timing FILE  display timing of the golden run (refmachine.write_vga_timing; default: boot environment)
//   --sync-ticks GOLDEN_DIR  at each stop, set the BIOS tick count (0040:006C-0070) from GOLDEN_DIR/Skkk.B
//   --break SSSS:OOOO[,...] --stop-keys "4D00,-,3920,..." ("-": no key) --dump-dir DIR [--stops N]
//   At each stop k (1-based): GC reads of the debugger dump are mirrored (3CE=4, 3CF=0..3), DIR/Skkk.D
//   (DGROUP 64 KB - 1), Skkk.B (BDA 256 B), Skkk.P0..P3 (planes, 28000 B) are written, then stop key k
//   is put in the BIOS buffer (as the debugger's SM commands do) and the run continues.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "machine_api.h"
#include "png.h"

int main(int argc, char** argv) {
    rc::MachineOptions o;
    double ms = 1000;
    double profile_from = -1;
    std::string keys, png, planes, bda_keys, stop_keys, dump_dir, sync_dir;
    std::vector<uint32_t> brks; int max_stops = 1 << 30;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--data") o.data_dir = next();
        else if (a == "--ms") ms = std::atof(next().c_str());
        else if (a == "--keys") keys = next();
        else if (a == "--png") png = next();
        else if (a == "--planes") planes = next();
        else if (a == "--missing") o.missing_log = next();
        else if (a == "--ticks") o.start_ticks = uint32_t(std::strtoul(next().c_str(), nullptr, 0));
        else if (a == "--trace-ints") o.trace_ints = true;
        else if (a == "--profile-from") profile_from = std::atof(next().c_str());
        else if (a == "--bda-keys") bda_keys = next();
        else if (a == "--stop-keys") stop_keys = next();
        else if (a == "--dump-dir") dump_dir = next();
        else if (a == "--sync-ticks") sync_dir = next();
        else if (a == "--speaker-io") o.speaker_io_log = next();
        else if (a == "--native") o.native = rc::MachineOptions::Native::On;
        else if (a == "--verify-native") o.native = rc::MachineOptions::Native::Verify;
        else if (a == "--pit0-phase") o.pit0_phase_ms = std::atof(next().c_str());
        else if (a == "--vga-timing") {  // display timing file (refmachine.write_vga_timing): 9 numbers per line
            if (FILE* fp = std::fopen(next().c_str(), "r")) {
                rc::VgaTiming v;
                while (std::fscanf(fp, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &v.from_ms, &v.t0_ms, &v.vtotal, &v.htotal,
                                   &v.hblkstart, &v.hblkend, &v.vrstart, &v.vrend, &v.vdend) == 9)
                    o.vga_timing.push_back(v);
                std::fclose(fp);
            }
        }
        else if (a == "--dos-time") {  // clock replay file: lines "SSSS:OOOO CX DX" (hex), as DOSBox-X REA_LOG_DOSTIME
            if (FILE* fp = std::fopen(next().c_str(), "r")) {
                unsigned cs, ip, cx, dx;
                while (std::fscanf(fp, "%x:%x %x %x", &cs, &ip, &cx, &dx) == 4)
                    o.dos_time_replay.push_back({(cs << 16) | ip, uint16_t(cx), uint16_t(dx)});
                std::fclose(fp);
            }
        }
        else if (a == "--stops") max_stops = std::atoi(next().c_str());
        else if (a == "--poke") {  // comma list of SSSS:OOOO=SSSS:OOOO:VVVV (at CS:IP, write word VVVV at seg:off)
            const std::string v = next();
            for (size_t q = 0; q + 24 <= v.size(); q += 25) {
                auto h = [&](size_t at, size_t n) { return unsigned(std::strtoul(v.substr(q + at, n).c_str(), nullptr, 16)); };
                o.pokes.push_back({h(0, 4) << 16 | h(5, 4), (h(10, 4) << 4) + h(15, 4), uint16_t(h(20, 4))});
            }
        }
        else if (a == "--break") {  // comma list of SSSS:OOOO
            const std::string v = next();
            for (size_t p = 0; p + 9 <= v.size(); p += 10)
                brks.push_back(uint32_t(std::strtoul(v.substr(p, 4).c_str(), nullptr, 16) << 16 | std::strtoul(v.substr(p + 5, 4).c_str(), nullptr, 16)));
        }
    }
    // keys: comma list of "ms:scancode" (scancode hex; make code, break is sent 60 ms later)
    struct Key { double t; uint8_t sc; };
    std::vector<Key> script;
    for (size_t p = 0; p < keys.size();) {
        size_t e = keys.find(',', p);
        std::string tok = keys.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? keys.size() : e + 1;
        size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        double t = std::atof(tok.substr(0, colon).c_str());
        uint8_t sc = uint8_t(std::strtoul(tok.substr(colon + 1).c_str(), nullptr, 16));
        script.push_back({t, sc});
        script.push_back({t + 60, uint8_t(sc | 0x80)});
    }
    // Lockstep stops are handled synchronously (also inside calls made from native procedures).
    std::function<void(bool)> stop_handler;
    o.on_break = [&](bool primary) { stop_handler(primary); };
    rc::Machine* m = rc::machine_create();
    std::string err;
    if (!rc::machine_init(m, o, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 2; }
    auto words = [](const std::string& list) {
        std::vector<uint16_t> v;
        for (size_t p = 0; p < list.size();) {
            size_t e = list.find(',', p);
            const std::string t = list.substr(p, e - p);
            v.push_back(t == "-" ? 0 : uint16_t(std::strtoul(t.c_str(), nullptr, 16)));  // "-": no key
            p = e == std::string::npos ? list.size() : e + 1;
        }
        return v;
    };
    uint8_t* ram = rc::machine_memory_rw(m);
    auto put_keys = [&](const std::vector<uint16_t>& ks) {  // buffer at 0040:001E, head 001E, tail after the keys
        unsigned a = 0x41E;
        for (uint16_t kw : ks) { ram[a] = uint8_t(kw); ram[a + 1] = uint8_t(kw >> 8); a += 2; }
        ram[0x41A] = 0x1E; ram[0x41B] = 0; ram[0x41C] = uint8_t(a - 0x400); ram[0x41D] = 0;
    };
    if (!bda_keys.empty()) put_keys(words(bda_keys));
    const std::vector<uint16_t> skeys = words(stop_keys);
    for (uint32_t b : brks) rc::machine_add_breakpoint(m, uint16_t(b >> 16), uint16_t(b));
    auto dump = [&](const std::string& f, const uint8_t* p, size_t n) {
        if (FILE* fp = std::fopen((dump_dir + "/" + f).c_str(), "wb")) { std::fwrite(p, 1, n, fp); std::fclose(fp); }
    };
    double now = 0;
    size_t k = 0;
    int stops = 0;
    // A stop: mirror the debugger's GC reads of the planes, write the dumps (primary only), then put stop key
    // k in the BIOS buffer and replay the tick count, as the golden capture does.
    stop_handler = [&](bool primary) {
        ++stops;
        char name[16];
        std::snprintf(name, sizeof name, "S%03d", stops);
        for (int p = 0; p < 4; ++p) { rc::machine_out8(m, 0x3CE, 4); rc::machine_out8(m, 0x3CF, uint8_t(p)); }
        if (primary) {
            dump(std::string(name) + ".D", ram + 0x257A0, 0xFFFF);
            dump(std::string(name) + ".B", ram + 0x400, 256);
            const uint32_t regs[2] = {rc::machine_cs_ip(m), rc::machine_ss_sp(m)};  // Skkk.R: CS:IP, SS:SP (port only)
            dump(std::string(name) + ".R", reinterpret_cast<const uint8_t*>(regs), sizeof regs);
            for (int p = 0; p < 4; ++p) dump(std::string(name) + ".P" + std::to_string(p), rc::machine_plane(m, p), 28000);
            std::printf("stop %d at %04X:%04X ss:sp=%04X:%04X t=%.1fms icount=%llu\n", stops, rc::machine_cs_ip(m) >> 16,
                        rc::machine_cs_ip(m) & 0xFFFF, rc::machine_ss_sp(m) >> 16, rc::machine_ss_sp(m) & 0xFFFF,
                        double(rc::machine_icount(m)) / o.instructions_per_ms, (unsigned long long)rc::machine_icount(m));
        }
        if (stops >= max_stops) { rc::machine_stop(m); return; }
        if (size_t(stops) <= skeys.size() && skeys[stops - 1]) put_keys({skeys[stops - 1]});
        if (!sync_dir.empty()) {  // clock input replay: the BIOS tick count the original had at this stop
            if (FILE* fp = std::fopen((sync_dir + "/" + name + ".B").c_str(), "rb")) {
                uint8_t b[256];
                if (std::fread(b, 1, 256, fp) == 256) std::memcpy(ram + 0x46C, b + 0x6C, 5);
                std::fclose(fp);
            }
        }
    };
    while (now < ms) {
        rc::RunState st = rc::machine_state(m);
        if (st != rc::RunState::Running) {
            break;
        }
        while (k < script.size() && script[k].t <= now) rc::machine_key(m, script[k++].sc);
        if (profile_from >= 0 && now >= profile_from) { rc::machine_profile(m, true); profile_from = 1e18; }
        const uint64_t before = rc::machine_icount(m);
        rc::machine_run_ms(m, 1.0);
        now += double(rc::machine_icount(m) - before) / o.instructions_per_ms;
    }
    {
        const uint8_t* mem = rc::machine_memory(m);
        auto w = [&](unsigned a) { return unsigned(mem[a] | (mem[a + 1] << 8)); };
        std::printf("kbd head=%04X tail=%04X ticks=%08X buf:", w(0x41A), w(0x41C), w(0x46C) | (w(0x46E) << 16));
        for (unsigned a = 0x41E; a < 0x43E; a += 2) std::printf(" %04X", w(a));
        std::printf("\n");
    }
    if (o.native == rc::MachineOptions::Native::Verify) std::printf("verified: %s\n", rc::machine_native_report(m).c_str());
    std::printf("state=%d icount=%llu msg=%s\n", int(rc::machine_state(m)),
                (unsigned long long)rc::machine_icount(m), rc::machine_message(m).c_str());
    if (profile_from >= 0)
        for (auto& e : rc::machine_profile_top(m, 12)) std::printf("hot %04X:%04X %llu\n", e.first >> 16, e.first & 0xFFFF, (unsigned long long)e.second);
    if (!png.empty()) {
        std::vector<uint8_t> idx; int w, h; uint32_t pal[16];
        rc::machine_render(m, idx, w, h, pal);
        std::vector<uint32_t> rgb(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) rgb[i] = pal[idx[i] & 15];
        write_png_argb(png, w, h, rgb);
    }
    if (!planes.empty()) {
        for (int p = 0; p < 4; ++p) {
            std::string f = planes + ".P" + std::to_string(p);
            if (FILE* fp = std::fopen(f.c_str(), "wb")) { std::fwrite(rc::machine_plane(m, p), 1, 28000, fp); std::fclose(fp); }
        }
    }
    rc::machine_destroy(m);
    return 0;
}
