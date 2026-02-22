#pragma once
#include <vector>
#include <cstdint>

namespace meshtile {

// Encode 256x256 RGBA buffer as palette+tRNS PNG (Google Earth compatible).
// Falls back to RGBA if >256 unique colors.
std::vector<uint8_t> encode_tile_png(const std::vector<uint8_t>& rgba);

// Pre-built fully transparent 256x256 PNG (palette+tRNS).
const std::vector<uint8_t>& transparent_tile_png();

} // namespace meshtile
