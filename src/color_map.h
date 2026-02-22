#pragma once
#include <cstdint>
#include <algorithm>
#include "types.h"

namespace meshtile {

struct RGBA { uint8_t r, g, b, a; };

namespace detail {

struct ColorStop { float t; uint8_t r, g, b; };

inline RGBA lerp_stops(const ColorStop* stops, int n, float t, float alpha) {
    if (t <= stops[0].t) return {stops[0].r, stops[0].g, stops[0].b,
                                  static_cast<uint8_t>(alpha * 255)};
    if (t >= stops[n - 1].t) return {stops[n-1].r, stops[n-1].g, stops[n-1].b,
                                      static_cast<uint8_t>(alpha * 255)};
    for (int i = 0; i < n - 1; ++i) {
        if (t >= stops[i].t && t <= stops[i + 1].t) {
            float f = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
            return {
                static_cast<uint8_t>(stops[i].r + f * (stops[i+1].r - stops[i].r)),
                static_cast<uint8_t>(stops[i].g + f * (stops[i+1].g - stops[i].g)),
                static_cast<uint8_t>(stops[i].b + f * (stops[i+1].b - stops[i].b)),
                static_cast<uint8_t>(alpha * 255)
            };
        }
    }
    return {0, 0, 0, 0};
}

inline RGBA red_yellow_green(float t, float alpha) {
    float r, g;
    if (t > 0.5f) { r = 2.0f * (1.0f - t); g = 1.0f; }
    else           { r = 1.0f;               g = 2.0f * t; }
    return {
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        0,
        static_cast<uint8_t>(alpha * 255)
    };
}

inline RGBA plasma(float t, float alpha) {
    static const ColorStop stops[] = {
        {0.000f,  13,   8, 135},
        {0.125f,  75,   3, 161},
        {0.250f, 125,   3, 168},
        {0.375f, 168,  34, 150},
        {0.500f, 203,  70, 121},
        {0.625f, 229, 107,  93},
        {0.750f, 248, 149,  64},
        {0.875f, 253, 195,  40},
        {1.000f, 240, 249,  33},
    };
    return lerp_stops(stops, 9, t, alpha);
}

inline RGBA viridis(float t, float alpha) {
    static const ColorStop stops[] = {
        {0.000f,  68,   1,  84},
        {0.125f,  72,  36, 117},
        {0.250f,  65,  68, 135},
        {0.375f,  53,  95, 141},
        {0.500f,  42, 120, 142},
        {0.625f,  33, 145, 140},
        {0.750f,  34, 168, 132},
        {0.875f,  68, 191, 112},
        {1.000f, 253, 231,  37},
    };
    return lerp_stops(stops, 9, t, alpha);
}

inline RGBA turbo(float t, float alpha) {
    static const ColorStop stops[] = {
        {0.000f,  48,  18,  59},
        {0.125f,  37,  85, 198},
        {0.250f,  16, 150, 230},
        {0.375f,  18, 205, 148},
        {0.500f,  81, 237,  62},
        {0.625f, 184, 243,  22},
        {0.750f, 244, 197,  22},
        {0.875f, 249, 123,  12},
        {1.000f, 122,   4,   3},
    };
    return lerp_stops(stops, 9, t, alpha);
}

inline RGBA inferno(float t, float alpha) {
    static const ColorStop stops[] = {
        {0.000f,   0,   0,   4},
        {0.125f,  40,  11,  84},
        {0.250f, 101,  21, 110},
        {0.375f, 159,  42,  99},
        {0.500f, 212,  72,  66},
        {0.625f, 245, 125,  21},
        {0.750f, 250, 186,  46},
        {0.875f, 237, 239, 115},
        {1.000f, 252, 255, 164},
    };
    return lerp_stops(stops, 9, t, alpha);
}

} // namespace detail

inline RGBA signal_to_rgba(float dbm, float min_dbm = -130.0f, float max_dbm = -80.0f,
                           Colormap cmap = Colormap::red_yellow_green) {
    if (dbm <= -999.0f || dbm < min_dbm) return {0, 0, 0, 0};

    float t = std::clamp((dbm - min_dbm) / (max_dbm - min_dbm), 0.0f, 1.0f);
    float alpha = 0.3f + 0.4f * t;

    switch (cmap) {
        case Colormap::plasma:           return detail::plasma(t, alpha);
        case Colormap::viridis:          return detail::viridis(t, alpha);
        case Colormap::turbo:            return detail::turbo(t, alpha);
        case Colormap::inferno:          return detail::inferno(t, alpha);
        case Colormap::red_yellow_green:
        default:                         return detail::red_yellow_green(t, alpha);
    }
}

} // namespace meshtile
