#pragma once
#include "signal_cache.h"
#include "types.h"
#include <vector>
#include <cstdint>

namespace meshtile {

class TileRenderer {
public:
    TileRenderer(SignalCache& cache, const RfConfig& rf_config);

    // Returns PNG bytes (always 256x256, transparent if no signal).
    std::vector<uint8_t> render_tile(int z, int x, int y) const;

private:
    SignalCache& m_cache;
    RfConfig m_rf_config;

    std::vector<uint8_t> render_fresh(int z, int x, int y) const;
};

} // namespace meshtile
