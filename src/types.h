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

struct RfConfig {
    float rx_sensitivity_dbm  = -130.0f;
    float rx_height_agl_m     = 1.0f;
    float rx_antenna_gain_dbi = 2.0f;
    float rx_cable_loss_db    = 2.0f;
    float display_min_dbm     = -130.0f;
    float display_max_dbm     = -80.0f;
};

struct SignalGrid {
    std::string node_id;
    Bounds bounds;
    int rows = 0, cols = 0;
    std::vector<float> signal; // row-major dBm, -999 = no signal
};

} // namespace meshtile
