// EGA/VGA adapter model for 16-colour planar modes (0Dh, 0Eh, 10h, 12h) and colour text mode 3.
// Register semantics follow the IBM EGA/VGA documentation (FreeVGA); write modes 0-2, read modes 0-1.
#pragma once
#include <cstdint>
#include <vector>

namespace rc {

struct Ega {
    uint8_t planes[4][0x10000] = {};
    uint8_t latch[4] = {};
    uint8_t seq_index = 0, seq[5] = {0x03, 0x00, 0x0F, 0x00, 0x06};
    uint8_t gc_index = 0, gc[9] = {0, 0, 0, 0, 0, 0, 0x05, 0x0F, 0xFF};
    uint8_t crtc_index = 0, crtc[0x19] = {};
    uint8_t attr_index = 0, attr[0x15] = {};
    bool attr_flipflop = false;        // false: next write to 3C0 is an index
    uint8_t misc_output = 0x63;
    uint8_t dac[256][3] = {};          // 6-bit components
    uint8_t dac_write = 0, dac_read = 0, dac_comp = 0;
    uint8_t dac_mask = 0xFF;
    int mode = 3;                      // current BIOS video mode
    bool graphics = false;

    void set_mode(int m, bool clear);
    uint8_t vram_read(uint32_t linear, uint8_t* textmem);
    void vram_write(uint32_t linear, uint8_t v, uint8_t* textmem);
    uint8_t port_read(uint16_t port, uint8_t status3da);  // status3da: display timing bits (machine)
    void port_write(uint16_t port, uint8_t v);
    // Pixel indices 0-15 (before the attribute palette) of the visible 640x350 / 640x480 screen.
    void render_indices(std::vector<uint8_t>& out, int& w, int& h) const;
    // RGB (8-bit per channel) of attribute index i through the attribute palette and the DAC.
    uint32_t rgb_of_index(int i) const;
    uint16_t start_address() const { return uint16_t((crtc[0x0C] << 8) | crtc[0x0D]); }
};

}  // namespace rc
