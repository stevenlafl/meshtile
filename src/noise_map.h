#pragma once
#include "types.h"
#include <vector>
#include <string>

namespace meshtile {

struct NoiseObservation {
    double lat, lon;
    float noisefloor_dbm;
};

class NoiseMap {
public:
    // Build from observations. Resolution in degrees (e.g. 0.005 ~ 500m).
    NoiseMap(const std::vector<NoiseObservation>& obs, double resolution = 0.005);

    // Sample noise floor at location. Returns default if no data nearby.
    float sample(double lat, double lon) const;

    float default_noisefloor() const { return m_default; }

private:
    Bounds m_bounds;
    int m_rows, m_cols;
    double m_resolution;
    float m_default;
    std::vector<float> m_grid;    // average noise floor per cell, -999 = no data
    std::vector<int>   m_counts;  // observation count per cell
};

// Parse noise observations from map_data JSON file or URL
std::vector<NoiseObservation> load_noise_observations(const std::string& source);

} // namespace meshtile
