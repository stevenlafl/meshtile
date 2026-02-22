#include "tile_renderer.h"
#include "tile_math.h"
#include "color_map.h"
#include "png_encode.h"

namespace meshtile {

TileRenderer::TileRenderer(SignalCache& cache, const RfConfig& rf_config,
                           const NoiseMap* noise_map, NodeRenderer* node_renderer)
    : m_cache(cache), m_rf_config(rf_config),
      m_noise_map(noise_map), m_node_renderer(node_renderer) {}

bool TileRenderer::render_signal_rgba(int z, int x, int y, std::vector<uint8_t>& rgba) const {
    Bounds bounds = tile_to_bounds(z, x, y);
    auto overlapping = m_cache.grids_overlapping(bounds);
    if (overlapping.empty()) return false;

    constexpr int SIZE = 256;
    bool has_signal = false;

    double lat_span = bounds.max_lat - bounds.min_lat;
    double lon_span = bounds.max_lon - bounds.min_lon;

    for (int py = 0; py < SIZE; ++py) {
        double lat = bounds.max_lat - (py + 0.5) / SIZE * lat_span;
        for (int px = 0; px < SIZE; ++px) {
            double lon = bounds.min_lon + (px + 0.5) / SIZE * lon_span;

            float dbm = m_cache.sample_grids(lat, lon, overlapping);

            float min_dbm = m_rf_config.display_min_dbm;
            if (m_noise_map) {
                float nf = m_noise_map->sample(lat, lon);
                min_dbm = std::max(min_dbm, nf);
            }

            RGBA color = signal_to_rgba(dbm, min_dbm,
                                        m_rf_config.display_max_dbm);

            if (color.a > 0) has_signal = true;

            int off = (py * SIZE + px) * 4;
            rgba[off + 0] = color.r;
            rgba[off + 1] = color.g;
            rgba[off + 2] = color.b;
            rgba[off + 3] = color.a;
        }
    }

    return has_signal;
}

std::vector<uint8_t> TileRenderer::render_signal(int z, int x, int y) const {
    constexpr int SIZE = 256;
    std::vector<uint8_t> rgba(SIZE * SIZE * 4, 0);

    if (!render_signal_rgba(z, x, y, rgba))
        return transparent_tile_png();

    return encode_tile_png(rgba);
}

std::vector<uint8_t> TileRenderer::render_tile(int z, int x, int y) const {
    constexpr int SIZE = 256;
    std::vector<uint8_t> rgba(SIZE * SIZE * 4, 0);

    bool has_signal = render_signal_rgba(z, x, y, rgba);

    bool has_nodes = false;
    if (m_node_renderer) {
        has_nodes = m_node_renderer->composite_onto(z, x, y, rgba);
    }

    if (!has_signal && !has_nodes)
        return transparent_tile_png();

    return encode_tile_png(rgba);
}

} // namespace meshtile
