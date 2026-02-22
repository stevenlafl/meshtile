#pragma once
#include <cstdint>
#include <algorithm>

namespace meshtile {

struct RGBA { uint8_t r, g, b, a; };

inline RGBA signal_to_rgba(float dbm, float min_dbm = -130.0f, float max_dbm = -80.0f) {
    if (dbm <= -999.0f || dbm < min_dbm) return {0, 0, 0, 0};

    float t = std::clamp((dbm - min_dbm) / (max_dbm - min_dbm), 0.0f, 1.0f);

    // red (weak) -> yellow (mid) -> green (strong)
    float r, g;
    if (t > 0.5f) { r = 2.0f * (1.0f - t); g = 1.0f; }
    else           { r = 1.0f;               g = 2.0f * t; }

    float alpha = 0.3f + 0.4f * t;

    return {
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        0,
        static_cast<uint8_t>(alpha * 255)
    };
}

} // namespace meshtile
