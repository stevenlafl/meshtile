#pragma once
#include "types.h"
#include <cmath>

namespace meshtile {

inline Bounds tile_to_bounds(int z, int x, int y) {
    int n = 1 << z;
    double lon_min = x / static_cast<double>(n) * 360.0 - 180.0;
    double lon_max = (x + 1) / static_cast<double>(n) * 360.0 - 180.0;
    double lat_max_rad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * y / static_cast<double>(n))));
    double lat_min_rad = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (y + 1) / static_cast<double>(n))));
    return {
        lat_min_rad * 180.0 / M_PI,
        lat_max_rad * 180.0 / M_PI,
        lon_min,
        lon_max
    };
}

} // namespace meshtile
