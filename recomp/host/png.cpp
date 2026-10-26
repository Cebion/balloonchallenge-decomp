#include "png.h"

#include <array>
#include <fstream>

namespace {

uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}

void chunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& body) {
    be32(out, uint32_t(body.size()));
    std::vector<uint8_t> tb(tag, tag + 4);
    tb.insert(tb.end(), body.begin(), body.end());
    out.insert(out.end(), tb.begin(), tb.end());
    be32(out, crc32(tb.data(), tb.size()) ^ 0xFFFFFFFFu);
}

}  // namespace

bool write_png_argb(const std::string& path, int w, int h, const std::vector<uint32_t>& argb) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (1 + size_t(w) * 3));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const uint32_t p = argb[size_t(y) * w + x];
            raw.push_back(uint8_t(p >> 16));
            raw.push_back(uint8_t(p >> 8));
            raw.push_back(uint8_t(p));
        }
    }
    // zlib stream with stored (uncompressed) deflate blocks.
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = raw.size() - pos > 65535 ? 65535 : raw.size() - pos;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(f);
}
