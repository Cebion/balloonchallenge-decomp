#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Minimal uncompressed (stored-deflate) PNG writer for headless screenshots. No dependencies.
bool write_png_argb(const std::string& path, int w, int h, const std::vector<uint32_t>& argb);
