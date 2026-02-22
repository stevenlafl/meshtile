#pragma once
#include <string>
#include <vector>

namespace meshtile {

struct Bounds {
    double min_lat, max_lat;
    double min_lon, max_lon;
};

struct Node {
    std::string id;
    std::string name;
    double lat = 0, lon = 0;
    float antenna_height_m   = 2.0f;
    float tx_power_dbm       = 20.0f;  // 0.1w = 20 dBm
    float antenna_gain_dbi   = 2.0f;
    float cable_loss_db      = 2.0f;
    float rx_sensitivity_dbm = -130.0f;
    float frequency_mhz      = 906.875f;
    float max_range_km       = 30.0f;
};

enum class Colormap { red_yellow_green, plasma, viridis, turbo, inferno };

struct RfConfig {
    float rx_sensitivity_dbm  = -130.0f;
    float rx_height_agl_m     = 1.0f;
    float rx_antenna_gain_dbi = 2.0f;
    float rx_cable_loss_db    = 2.0f;
    float display_min_dbm     = -130.0f;
    float display_max_dbm     = -80.0f;

    // ITM environment parameters
    int    climate              = 5;       // CLIMATE__CONTINENTAL_TEMPERATE
    double refractivity         = 301.0;   // N-units
    int    polarization         = 1;       // POLARIZATION__VERTICAL
    double ground_dielectric    = 15.0;
    double ground_conductivity  = 0.005;   // S/m
    float  clutter_height_m     = 0.0f;    // ground clutter (trees/buildings)
    double time_pct             = 50.0;    // ITM time variability %
    double location_pct         = 50.0;    // ITM location variability %
    double situation_pct        = 50.0;    // ITM situation variability %

    Colormap colormap = Colormap::plasma;
};

struct SignalGrid {
    std::string node_id;
    Bounds bounds;
    int rows = 0, cols = 0;
    std::vector<float> signal; // row-major dBm, -999 = no signal
};

} // namespace meshtile
