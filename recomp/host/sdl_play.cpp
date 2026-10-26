// Playable SDL2 host for the recompiled game.
//   rc_play --data DIR [--scale N] [--fullscreen] [--speed INSNS_PER_MS] [--record SESSION.json] [--poke LIST]
// --record: lockstep recording. The game pauses at each key wait (runtime blocking wait 19AE:59D3, turn input
// 0824:22FC, key-from-set prompts 1932:005A) until you press a key; that key goes into the BIOS buffer (as
// tools/lockstep.py golden does) and is appended to SESSION.json (rewritten after every key). F12 at a stop
// records "-" (no key) and continues: polls that only look for a key (the options menu cycles options).
// --poke SSSS:OOOO=SSSS:OOOO:VVVV[,...]: one-shot word writes when execution reaches CS:IP (test states such
// as the forced registration flag, see record.sh); saved in SESSION.json as "pokes".
// --speed: CPU speed (default 8000 = the DOSBox-X reference; the original ran on 286..486 PCs of many
// speeds, so only CPU-paced parts change). The display timing (retrace) is the reference machine's (boot environment).
// Runs the machine in real time, shows the EGA screen (640x350 stretched to 4:3), sends key presses as
// PC scancodes (IRQ1 -> the reference INT 9 stub) and plays the PC speaker as a square wave.
#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gamefiles.h"
#include "machine_api.h"

namespace {

// Speaker events (emulated ms, frequency Hz or 0) played at their emulated times, kLatencyMs behind the
// machine, so tones of a few ms (the game steps pitch every 2-20 ms) keep their exact order and length.
// The audio clock follows the machine clock (rate correction over kFollowMs), so a machine running a little
// slower than real time (slow handhelds) plays its tones slower too instead of running dry and resyncing.
struct Tone { double ms; uint32_t freq; };
std::mutex g_tone_mutex;
std::deque<Tone> g_tones;
std::atomic<double> g_machine_ms{0};
constexpr double kLatencyMs = 60;
constexpr double kFollowMs = 250;

void audio_callback(void*, Uint8* stream, int len) {
    static double phase = 0, audio_ms = -1;
    static uint32_t freq = 0;
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    const double target = g_machine_ms.load() - kLatencyMs;
    if (audio_ms < 0 || audio_ms < target - 4 * kLatencyMs || audio_ms > target + kLatencyMs) audio_ms = target;  // resync
    double rate = 1.0 + (target - audio_ms) / kFollowMs;
    rate = rate < 0.5 ? 0.5 : (rate > 1.5 ? 1.5 : rate);
    std::lock_guard<std::mutex> lock(g_tone_mutex);
    for (int i = 0; i < len / 2; ++i) {
        while (!g_tones.empty() && g_tones.front().ms <= audio_ms) { freq = g_tones.front().freq; g_tones.pop_front(); }
        audio_ms += rate * 1000.0 / 44100.0;
        if (freq == 0 || freq > 20000) { out[i] = 0; continue; }
        phase += double(freq) / 44100.0;
        phase -= double(int(phase));
        out[i] = phase < 0.5 ? 3000 : -3000;
    }
}

// SDL scancode -> PC/XT set 1 make code (0 = not mapped). Extended keys get the E0 prefix.
struct Pc { uint8_t code; bool ext; };
Pc pc_scancode(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        static const uint8_t letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return {letters[s - SDL_SCANCODE_A], false};
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return {uint8_t(0x02 + (s - SDL_SCANCODE_1)), false};
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return {uint8_t(0x3B + (s - SDL_SCANCODE_F1)), false};
    switch (s) {
        case SDL_SCANCODE_ESCAPE: return {0x01, false};
        case SDL_SCANCODE_RETURN: return {0x1C, false};
        case SDL_SCANCODE_SPACE: return {0x39, false};
        case SDL_SCANCODE_BACKSPACE: return {0x0E, false};
        case SDL_SCANCODE_TAB: return {0x0F, false};
        case SDL_SCANCODE_MINUS: return {0x0C, false};
        case SDL_SCANCODE_EQUALS: return {0x0D, false};
        case SDL_SCANCODE_LSHIFT: return {0x2A, false};
        case SDL_SCANCODE_RSHIFT: return {0x36, false};
        case SDL_SCANCODE_LCTRL: return {0x1D, false};
        case SDL_SCANCODE_LALT: return {0x38, false};
        case SDL_SCANCODE_UP: return {0x48, true};
        case SDL_SCANCODE_DOWN: return {0x50, true};
        case SDL_SCANCODE_LEFT: return {0x4B, true};
        case SDL_SCANCODE_RIGHT: return {0x4D, true};
        case SDL_SCANCODE_HOME: return {0x47, true};
        case SDL_SCANCODE_END: return {0x4F, true};
        case SDL_SCANCODE_PAGEUP: return {0x49, true};
        case SDL_SCANCODE_PAGEDOWN: return {0x51, true};
        default: return {0, false};
    }
}

// Key word the BIOS would buffer (scan << 8 | ascii), keypad-style arrows (ascii 0); 0 = not supported.
uint16_t key_word(SDL_Scancode s, bool shift) {
    const Pc k = pc_scancode(s);
    if (!k.code) return 0;
    char a = 0;
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) a = char((shift ? 'A' : 'a') + (s - SDL_SCANCODE_A));
    else if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) a = (shift ? "!@#$%^&*()" : "1234567890")[s - SDL_SCANCODE_1];  // US layout, as the BIOS
    else if (s == SDL_SCANCODE_SPACE) a = ' ';
    else if (s == SDL_SCANCODE_RETURN) a = 13;
    else if (s == SDL_SCANCODE_ESCAPE) a = 27;
    else if (s == SDL_SCANCODE_BACKSPACE) a = 8;
    else if (s == SDL_SCANCODE_TAB) a = 9;
    else if (s == SDL_SCANCODE_MINUS) a = '-';
    else if (s == SDL_SCANCODE_LSHIFT || s == SDL_SCANCODE_RSHIFT || s == SDL_SCANCODE_LCTRL || s == SDL_SCANCODE_LALT) return 0;
    return uint16_t((k.code << 8) | uint8_t(a));
}

std::string g_pokes;  // --poke list, saved with the session so the golden capture applies the same writes

void save_session(const std::string& path, const std::vector<uint16_t>& keys) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\"bda_keys\": [], \"breaks\": [\"19AE:59D3\", \"0824:22FC\", \"1932:005A\", \"0824:B143\", \"0824:B5CF\"],\n \"stop_keys\": [");
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i]) std::fprintf(f, "%s\"%04X\"", i ? ", " : "", keys[i]);
        else std::fprintf(f, "%s\"-\"", i ? ", " : "");
    std::fprintf(f, "],\n \"pokes\": [");
    for (size_t q = 0; q + 24 <= g_pokes.size(); q += 25) std::fprintf(f, "%s\"%s\"", q ? ", " : "", g_pokes.substr(q, 24).c_str());
    std::fprintf(f, "],\n \"stops\": %zu}\n", keys.size());
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    rc::MachineOptions o;
    int scale = 2;
    bool fullscreen = false;   // the whole screen, 4:3 kept by the logical size
    std::string record;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) o.data_dir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (a == "--speed" && i + 1 < argc) o.instructions_per_ms = std::atof(argv[++i]);
        else if (a == "--record" && i + 1 < argc) record = argv[++i];
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--poke" && i + 1 < argc) {  // as rc_headless: SSSS:OOOO=SSSS:OOOO:VVVV[,...]
            const std::string v = g_pokes = argv[++i];
            for (size_t q = 0; q + 24 <= v.size(); q += 25) {
                auto h = [&](size_t at, size_t n) { return unsigned(std::strtoul(v.substr(q + at, n).c_str(), nullptr, 16)); };
                o.pokes.push_back({h(0, 4) << 16 | h(5, 4), (h(10, 4) << 4) + h(15, 4), uint16_t(h(20, 4))});
            }
        }
    }
    if (o.data_dir.empty()) { std::fprintf(stderr, "usage: rc_play --data DIR [--scale N] [--speed INSNS_PER_MS]\n"); return 2; }
    if (record.empty()) {  // recordings keep their own test state (--poke); normal play checks the player's files
        // The BIOS clock starts at the local time of day, as on a PC (the game seeds its boards with RANDOMIZE
        // TIMER); recordings keep the reference capture's clock so their replays match.
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm lt{};
        localtime_r(&t, &lt);
        const double frac = std::chrono::duration<double>(now - std::chrono::system_clock::from_time_t(t)).count();
        const double secs = lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec + frac;
        o.start_ticks = uint32_t(secs * (1193182.0 / 65536.0));
        const host::GameFiles gf = host::check_game_files(o.data_dir, o);
        std::fprintf(stderr, "%s\n", gf.message.c_str());
        if (gf.edition == host::Edition::Unusable) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Balloon Challenge", gf.message.c_str(), nullptr);
            return 2;
        }
    }
    rc::Machine* m = rc::machine_create();
    std::string err;
    if (!rc::machine_init(m, o, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 2; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("Balloon Challenge (recompiled)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640 * scale, 480 * scale, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE);
    if (fullscreen) SDL_ShowCursor(SDL_DISABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, 640, 480);
    SDL_Texture* tex = nullptr;
    int tex_w = 0, tex_h = 0;

    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_callback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
    std::vector<uint16_t> recorded;
    std::string allowed;  // key-from-set prompt: accepted (uppercased) characters
    if (!record.empty()) {
        rc::machine_add_breakpoint(m, 0x19AE, 0x59D3);
        rc::machine_add_breakpoint(m, 0x0824, 0x22FC);
        rc::machine_add_breakpoint(m, 0x1932, 0x005A);
        rc::machine_add_breakpoint(m, 0x0824, 0xB143);  // other key-poll call sites (options menu)
        rc::machine_add_breakpoint(m, 0x0824, 0xB5CF);
        save_session(record, recorded);
    }
    uint8_t* ram = rc::machine_memory_rw(m);

    std::vector<uint8_t> idx, prev_idx;
    std::vector<uint32_t> rgb;
    uint32_t pal[16], prev_pal[16] = {};
    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (rc::machine_state(m) == rc::RunState::Break) {
                if (e.type != SDL_KEYDOWN || e.key.repeat) continue;
                if (e.key.keysym.scancode == SDL_SCANCODE_F12) {  // no key at this stop: continue
                    recorded.push_back(0);
                    save_session(record, recorded);
                    std::printf("stop %zu: no key\n", recorded.size());
                    allowed.clear();
                    rc::machine_run_ms(m, 0);
                    SDL_SetWindowTitle(win, "Balloon Challenge (recording)");
                    last = SDL_GetPerformanceCounter();
                    continue;
                }
                const uint16_t w = key_word(e.key.keysym.scancode, (e.key.keysym.mod & KMOD_SHIFT) != 0);
                if (!w) continue;
                char up = char(w & 0xFF);
                if (up >= 'a' && up <= 'z') up = char(up - 32);
                if (!allowed.empty() && allowed.find(up) == std::string::npos) {
                    std::printf("key not accepted here (allowed: %s)\n", allowed.c_str());
                    continue;
                }
                ram[0x41E] = uint8_t(w); ram[0x41F] = uint8_t(w >> 8);
                ram[0x41A] = 0x1E; ram[0x41B] = 0; ram[0x41C] = 0x20; ram[0x41D] = 0;
                recorded.push_back(w);
                save_session(record, recorded);
                std::printf("stop %zu: key %04X\n", recorded.size(), w);
                allowed.clear();
                rc::machine_run_ms(m, 0);  // leave the stop (the breakpoint is passed once on resume)
                SDL_SetWindowTitle(win, "Balloon Challenge (recording)");
                last = SDL_GetPerformanceCounter();
                continue;
            }
            if (!record.empty() && (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)) continue;  // only at key waits
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {  // repeats too: typematic make codes, as a PC keyboard
                const Pc k = pc_scancode(e.key.keysym.scancode);
                if (!k.code) continue;
                if (k.ext) rc::machine_key(m, 0xE0);
                rc::machine_key(m, uint8_t(k.code | (e.type == SDL_KEYUP ? 0x80 : 0)));
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        double ms = double(now - last) * 1000.0 / double(SDL_GetPerformanceFrequency());
        last = now;
        if (ms > 50) ms = 50;  // after a stall, do not race to catch up
        rc::RunState st = rc::machine_state(m);
        if (st != rc::RunState::Break) st = rc::machine_run_ms(m, ms);
        if (st == rc::RunState::Break && rc::machine_cs_ip(m) == 0x1932005Au && allowed.empty()) {
            // key-from-set prompt: the allowed list is the string descriptor at [SP+6] (length, offset in DS)
            const uint32_t sssp = rc::machine_ss_sp(m);
            const uint32_t sp = (sssp >> 16) * 16u + (sssp & 0xFFFF);
            const uint16_t desc = uint16_t(ram[sp + 6] | (ram[sp + 7] << 8));
            const uint32_t ds = 0x257A0;  // DGROUP
            const unsigned len = ram[ds + desc] | (ram[ds + desc + 1] << 8);
            const unsigned off = ram[ds + desc + 2] | (ram[ds + desc + 3] << 8);
            for (unsigned i = 0; i < len && i < 32; ++i) allowed.push_back(char(ram[ds + off + i]));
            SDL_SetWindowTitle(win, ("Balloon Challenge: waiting for one of: " + allowed).c_str());
        } else if (st == rc::RunState::Break) {
            SDL_SetWindowTitle(win, "Balloon Challenge: waiting for your key (F12 = no key, continue)");
        }
        {
            std::lock_guard<std::mutex> lock(g_tone_mutex);
            for (const auto& ev : rc::machine_take_speaker(m)) g_tones.push_back({double(ev.first) / o.instructions_per_ms, ev.second});
        }
        g_machine_ms = double(rc::machine_icount(m)) / o.instructions_per_ms;
        if (st != rc::RunState::Running && st != rc::RunState::Break) {
            std::printf("%s\n", rc::machine_message(m).c_str());
            running = false;
        }
        int w, h;
        rc::machine_render(m, idx, w, h, pal);
        const bool changed = !tex || w != tex_w || h != tex_h || idx != prev_idx || std::memcmp(pal, prev_pal, sizeof pal) != 0;
        if (w != tex_w || h != tex_h) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            tex_w = w; tex_h = h;
        }
        if (changed) {  // unchanged screens keep the uploaded texture (conversion and upload are slow on handhelds)
            rgb.resize(idx.size());
            for (size_t i = 0; i < idx.size(); ++i) rgb[i] = 0xFF000000u | pal[idx[i] & 15];
            SDL_UpdateTexture(tex, nullptr, rgb.data(), w * 4);
            prev_idx.swap(idx);
            std::memcpy(prev_pal, pal, sizeof pal);
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    rc::machine_destroy(m);
    return 0;
}
