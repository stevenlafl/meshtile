#include "png_encode.h"
#include <lodepng.h>
#include <unordered_map>

namespace meshtile {

static constexpr int TILE_SIZE = 256;

std::vector<uint8_t> encode_tile_png(const std::vector<uint8_t>& rgba) {
    // Collect unique RGBA colors
    struct ColorHash {
        size_t operator()(uint32_t c) const { return std::hash<uint32_t>{}(c); }
    };
    std::unordered_map<uint32_t, uint8_t, ColorHash> palette_map;
    const int npixels = TILE_SIZE * TILE_SIZE;

    for (int i = 0; i < npixels; ++i) {
        int off = i * 4;
        uint32_t key = (uint32_t(rgba[off]) << 24) | (uint32_t(rgba[off+1]) << 16)
                     | (uint32_t(rgba[off+2]) << 8)  | uint32_t(rgba[off+3]);
        if (palette_map.find(key) == palette_map.end()) {
            if (palette_map.size() >= 256) {
                // Too many colors, fall back to RGBA
                std::vector<uint8_t> png;
                lodepng::encode(png, rgba, TILE_SIZE, TILE_SIZE);
                return png;
            }
            palette_map[key] = static_cast<uint8_t>(palette_map.size());
        }
    }

    // Build palette arrays
    int ncolors = static_cast<int>(palette_map.size());
    std::vector<uint8_t> pal_r(ncolors), pal_g(ncolors), pal_b(ncolors), pal_a(ncolors);
    for (auto& [key, idx] : palette_map) {
        pal_r[idx] = (key >> 24) & 0xFF;
        pal_g[idx] = (key >> 16) & 0xFF;
        pal_b[idx] = (key >>  8) & 0xFF;
        pal_a[idx] =  key        & 0xFF;
    }

    // Build indexed pixel data
    std::vector<uint8_t> indexed(npixels);
    for (int i = 0; i < npixels; ++i) {
        int off = i * 4;
        uint32_t key = (uint32_t(rgba[off]) << 24) | (uint32_t(rgba[off+1]) << 16)
                     | (uint32_t(rgba[off+2]) << 8)  | uint32_t(rgba[off+3]);
        indexed[i] = palette_map[key];
    }

    // Encode as palette PNG with tRNS
    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    state.encoder.auto_convert = 0;

    for (int i = 0; i < ncolors; ++i) {
        lodepng_palette_add(&state.info_png.color, pal_r[i], pal_g[i], pal_b[i], pal_a[i]);
        lodepng_palette_add(&state.info_raw, pal_r[i], pal_g[i], pal_b[i], pal_a[i]);
    }

    std::vector<uint8_t> png;
    lodepng::encode(png, indexed, TILE_SIZE, TILE_SIZE, state);
    return png;
}

const std::vector<uint8_t>& transparent_tile_png() {
    static const auto png = [] {
        std::vector<uint8_t> pixels(TILE_SIZE * TILE_SIZE, 0);
        std::vector<uint8_t> out;
        lodepng::State state;
        state.info_raw.colortype = LCT_PALETTE;
        state.info_raw.bitdepth = 1;
        state.info_png.color.colortype = LCT_PALETTE;
        state.info_png.color.bitdepth = 1;
        lodepng_palette_add(&state.info_png.color, 0, 0, 0, 0);
        lodepng_palette_add(&state.info_raw, 0, 0, 0, 0);
        state.encoder.auto_convert = 0;
        lodepng::encode(out, pixels, TILE_SIZE, TILE_SIZE, state);
        return out;
    }();
    return png;
}

} // namespace meshtile
