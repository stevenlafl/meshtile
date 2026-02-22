#pragma once
#include "signal_cache.h"
#include "node_renderer.h"
#include "noise_map.h"
#include "types.h"
#include <vector>
#include <cstdint>

namespace meshtile {

class TileRenderer {
public:
    TileRenderer(SignalCache& cache, const RfConfig& rf_config,
                 const NoiseMap* noise_map = nullptr,
                 NodeRenderer* node_renderer = nullptr);

    // Signal + nodes combined (for /tiles/)
    std::vector<uint8_t> render_tile(int z, int x, int y) const;

    // Signal only (for /signal/)
    std::vector<uint8_t> render_signal(int z, int x, int y) const;

private:
    SignalCache& m_cache;
    RfConfig m_rf_config;
    const NoiseMap* m_noise_map;
    NodeRenderer* m_node_renderer;

    // Render signal into RGBA buffer, returns true if any signal present
    bool render_signal_rgba(int z, int x, int y, std::vector<uint8_t>& rgba) const;
};

} // namespace meshtile
