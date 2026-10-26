#include "ega.h"

#include <array>
#include <cstring>

namespace rc {

namespace {
// Standard EGA attribute palette for 16-colour modes and the matching 64-entry DAC (rgbRGB -> 6 bit).
const uint8_t kEgaPalette[16] = {0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};
uint8_t six(int hi, int lo) { return uint8_t(0x2A * hi + 0x15 * lo); }
}  // namespace

void Ega::set_mode(int m, bool clear) {
    mode = m;
    graphics = (m == 0x0D || m == 0x0E || m == 0x10 || m == 0x12);
    // DAC: VGA BIOS loads the 64-colour EGA-compatible table into entries 0-63 for EGA modes.
    for (int i = 0; i < 64; ++i) {
        dac[i][0] = six((i >> 2) & 1, (i >> 5) & 1);
        dac[i][1] = six((i >> 1) & 1, (i >> 4) & 1);
        dac[i][2] = six(i & 1, (i >> 3) & 1);
    }
    for (int i = 0; i < 16; ++i) attr[i] = kEgaPalette[i];
    attr[0x10] = graphics ? 0x01 : 0x0C;  // mode control
    attr[0x11] = 0x00;                    // overscan
    attr[0x12] = 0x0F;                    // colour plane enable
    attr[0x13] = 0x00;
    attr[0x14] = 0x00;
    seq[2] = 0x0F;
    seq[4] = graphics ? 0x06 : 0x02;
    std::memset(gc, 0, sizeof gc);
    gc[5] = graphics ? 0x00 : 0x10;
    gc[6] = graphics ? 0x05 : 0x0E;
    gc[7] = 0x0F;
    gc[8] = 0xFF;
    std::memset(crtc, 0, sizeof crtc);
    crtc[0x13] = 40;                      // offset: 80 bytes per scanline
    if (clear) std::memset(planes, 0, sizeof planes);
}

static uint8_t rotate(uint8_t v, int n) { n &= 7; return uint8_t((v >> n) | (v << (8 - n))); }

uint8_t Ega::vram_read(uint32_t linear, uint8_t* textmem) {
    if (!graphics || linear >= 0xB0000) return textmem[linear];
    const uint32_t off = linear - 0xA0000;
    for (int p = 0; p < 4; ++p) latch[p] = planes[p][off];
    if (gc[5] & 0x08) {  // read mode 1: colour compare
        uint8_t res = 0xFF;
        for (int p = 0; p < 4; ++p) {
            if (!(gc[7] & (1 << p))) continue;
            const uint8_t want = (gc[2] & (1 << p)) ? 0xFF : 0x00;
            res &= uint8_t(~(latch[p] ^ want));
        }
        return res;
    }
    return latch[gc[4] & 3];
}

void Ega::vram_write(uint32_t linear, uint8_t v, uint8_t* textmem) {
    if (!graphics || linear >= 0xB0000) { textmem[linear] = v; return; }
    const uint32_t off = linear - 0xA0000;
    const int wmode = gc[5] & 3;
    const uint8_t func = (gc[3] >> 3) & 3;
    const uint8_t mask = gc[8];
    for (int p = 0; p < 4; ++p) {
        if (!(seq[2] & (1 << p))) continue;
        uint8_t val;
        if (wmode == 1) {
            planes[p][off] = latch[p];
            continue;
        } else if (wmode == 2) {
            val = (v & (1 << p)) ? 0xFF : 0x00;
        } else {
            val = (gc[1] & (1 << p)) ? ((gc[0] & (1 << p)) ? 0xFF : 0x00) : rotate(v, gc[3] & 7);
        }
        switch (func) {
            case 1: val &= latch[p]; break;
            case 2: val |= latch[p]; break;
            case 3: val ^= latch[p]; break;
            default: break;
        }
        planes[p][off] = uint8_t((val & mask) | (latch[p] & ~mask));
    }
}

uint8_t Ega::port_read(uint16_t port, uint8_t status3da) {
    switch (port) {
        case 0x3C4: return seq_index;
        case 0x3C5: return seq_index < 5 ? seq[seq_index] : 0;
        case 0x3CE: return gc_index;
        case 0x3CF: return gc_index < 9 ? gc[gc_index] : 0;
        case 0x3D4: return crtc_index;
        case 0x3D5: return crtc_index < 0x19 ? crtc[crtc_index] : 0;
        case 0x3C0: return attr_index;
        case 0x3C1: return attr_index < 0x15 ? attr[attr_index] : 0;
        case 0x3CC: return misc_output;
        case 0x3C6: return dac_mask;
        case 0x3C7: return 0;
        case 0x3C8: return dac_write;
        case 0x3C9: {
            uint8_t v = dac[dac_read][dac_comp];
            if (++dac_comp == 3) { dac_comp = 0; ++dac_read; }
            return v;
        }
        case 0x3DA:
            attr_flipflop = false;
            return status3da;
        default: return 0xFF;
    }
}

void Ega::port_write(uint16_t port, uint8_t v) {
    switch (port) {
        case 0x3C4: seq_index = v; break;
        case 0x3C5: if (seq_index < 5) seq[seq_index] = v; break;
        case 0x3CE: gc_index = v; break;
        case 0x3CF: if (gc_index < 9) gc[gc_index] = v; break;
        case 0x3D4: crtc_index = v; break;
        case 0x3D5: if (crtc_index < 0x19) crtc[crtc_index] = v; break;
        case 0x3C0:
            if (!attr_flipflop) attr_index = v & 0x1F;
            else if (attr_index < 0x15) attr[attr_index] = v;
            attr_flipflop = !attr_flipflop;
            break;
        case 0x3C2: misc_output = v; break;
        case 0x3C6: dac_mask = v; break;
        case 0x3C7: dac_read = v; dac_comp = 0; break;
        case 0x3C8: dac_write = v; dac_comp = 0; break;
        case 0x3C9:
            dac[dac_write][dac_comp] = v & 0x3F;
            if (++dac_comp == 3) { dac_comp = 0; ++dac_write; }
            break;
        default: break;
    }
}

void Ega::render_indices(std::vector<uint8_t>& out, int& w, int& h) const {
    w = 640;
    h = (mode == 0x12) ? 480 : (mode == 0x10 ? 350 : 200);
    out.assign(size_t(w) * h, 0);
    if (!graphics) return;
    const unsigned stride = crtc[0x13] * 2u ? crtc[0x13] * 2u : 80u;
    const unsigned start = start_address();
    // Eight pixels per plane byte: spread[b] holds bit 7..0 of b as bytes 0..7 (pixel order, little-endian).
    static const auto spread = [] {
        std::array<uint64_t, 256> t{};
        for (int b = 0; b < 256; ++b)
            for (int i = 0; i < 8; ++i) t[b] |= uint64_t((b >> (7 - i)) & 1) << (8 * i);
        return t;
    }();
    const uint64_t mask = (attr[0x12] & 0x0Fu) * 0x0101010101010101ull;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = out.data() + size_t(y) * w;
        for (int xb = 0; xb < w / 8; ++xb) {
            const unsigned off = (start + y * stride + xb) & 0xFFFF;
            const uint64_t v = spread[planes[0][off]] | spread[planes[1][off]] << 1 | spread[planes[2][off]] << 2 |
                               spread[planes[3][off]] << 3;
            const uint64_t px = v & mask;
            std::memcpy(row + xb * 8, &px, 8);
        }
    }
}

uint32_t Ega::rgb_of_index(int i) const {
    const uint8_t d = attr[i & 15] & 0x3F;
    auto c8 = [](uint8_t c6) { return uint32_t((c6 << 2) | (c6 >> 4)); };
    return (c8(dac[d][0]) << 16) | (c8(dac[d][1]) << 8) | c8(dac[d][2]);
}

}  // namespace rc
