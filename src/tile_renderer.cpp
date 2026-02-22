#include "tile_renderer.h"
#include "tile_math.h"
#include "color_map.h"
#include <lodepng.h>

namespace meshtile {

TileRenderer::TileRenderer(SignalCache& cache, const RfConfig& rf_config)
    : m_cache(cache), m_rf_config(rf_config) {}

std::vector<uint8_t> TileRenderer::render_tile(int z, int x, int y) const {
    return render_fresh(z, x, y);
}

// 256x256 fully transparent PNG using palette + tRNS (like openrailwaymap).
// Google Earth doesn't handle RGBA transparency well for tile overlays,
// but palette-based PNGs with tRNS chunks work correctly.
static std::vector<uint8_t> make_transparent_png() {
    constexpr int SIZE = 256;
    // 1-bit palette, all pixels = index 0, palette[0] = black, tRNS[0] = 0 (fully transparent)
    std::vector<uint8_t> pixels(SIZE * SIZE, 0);
    std::vector<uint8_t> png;
    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 1;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 1;
    // Add single palette entry: black with alpha=0
    lodepng_palette_add(&state.info_png.color, 0, 0, 0, 0);
    lodepng_palette_add(&state.info_raw, 0, 0, 0, 0);
    state.encoder.auto_convert = 0;  // don't auto-convert, use our palette
    lodepng::encode(png, pixels, SIZE, SIZE, state);
    return png;
}

static const std::vector<uint8_t>& transparent_png() {
    static auto png = make_transparent_png();
    return png;
}

std::vector<uint8_t> TileRenderer::render_fresh(int z, int x, int y) const {
    Bounds bounds = tile_to_bounds(z, x, y);

    auto overlapping = m_cache.grids_overlapping(bounds);
    if (overlapping.empty()) return transparent_png();

    constexpr int SIZE = 256;
    std::vector<uint8_t> rgba(SIZE * SIZE * 4, 0);
    bool has_signal = false;

    double lat_span = bounds.max_lat - bounds.min_lat;
    double lon_span = bounds.max_lon - bounds.min_lon;

    for (int py = 0; py < SIZE; ++py) {
        double lat = bounds.max_lat - (py + 0.5) / SIZE * lat_span;
        for (int px = 0; px < SIZE; ++px) {
            double lon = bounds.min_lon + (px + 0.5) / SIZE * lon_span;

            float dbm = m_cache.sample_grids(lat, lon, overlapping);
            RGBA color = signal_to_rgba(dbm,
                                        m_rf_config.display_min_dbm,
                                        m_rf_config.display_max_dbm);

            if (color.a > 0) has_signal = true;

            int off = (py * SIZE + px) * 4;
            rgba[off + 0] = color.r;
            rgba[off + 1] = color.g;
            rgba[off + 2] = color.b;
            rgba[off + 3] = color.a;
        }
    }

    if (!has_signal) return transparent_png();

    std::vector<uint8_t> png;
    lodepng::encode(png, rgba, SIZE, SIZE);
    return png;
}

} // namespace meshtile
