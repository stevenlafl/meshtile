#include "noise_map.h"
#include "http_fetch.h"
#include "log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace meshtile {

NoiseMap::NoiseMap(const std::vector<NoiseObservation>& obs, double resolution)
    : m_resolution(resolution), m_default(-110.0f)
{
    if (obs.empty()) {
        m_bounds = {0, 0, 0, 0};
        m_rows = m_cols = 0;
        return;
    }

    // Compute median as default for areas with no data
    std::vector<float> vals;
    vals.reserve(obs.size());
    for (const auto& o : obs) vals.push_back(o.noisefloor_dbm);
    std::sort(vals.begin(), vals.end());
    m_default = vals[vals.size() / 2];

    // Grid bounds with padding
    m_bounds.min_lat = obs[0].lat;
    m_bounds.max_lat = obs[0].lat;
    m_bounds.min_lon = obs[0].lon;
    m_bounds.max_lon = obs[0].lon;
    for (const auto& o : obs) {
        m_bounds.min_lat = std::min(m_bounds.min_lat, o.lat);
        m_bounds.max_lat = std::max(m_bounds.max_lat, o.lat);
        m_bounds.min_lon = std::min(m_bounds.min_lon, o.lon);
        m_bounds.max_lon = std::max(m_bounds.max_lon, o.lon);
    }
    // Pad by one cell
    m_bounds.min_lat -= resolution;
    m_bounds.max_lat += resolution;
    m_bounds.min_lon -= resolution;
    m_bounds.max_lon += resolution;

    m_rows = static_cast<int>((m_bounds.max_lat - m_bounds.min_lat) / resolution) + 1;
    m_cols = static_cast<int>((m_bounds.max_lon - m_bounds.min_lon) / resolution) + 1;

    m_grid.assign(m_rows * m_cols, 0.0f);
    m_counts.assign(m_rows * m_cols, 0);

    // Accumulate observations into grid cells
    for (const auto& o : obs) {
        int r = static_cast<int>((m_bounds.max_lat - o.lat) / resolution);
        int c = static_cast<int>((o.lon - m_bounds.min_lon) / resolution);
        r = std::clamp(r, 0, m_rows - 1);
        c = std::clamp(c, 0, m_cols - 1);
        int idx = r * m_cols + c;
        m_grid[idx] += o.noisefloor_dbm;
        m_counts[idx]++;
    }

    // Average
    for (int i = 0; i < m_rows * m_cols; ++i) {
        if (m_counts[i] > 0)
            m_grid[i] /= m_counts[i];
        else
            m_grid[i] = -999.0f; // no data
    }

    int filled = 0;
    for (int i = 0; i < m_rows * m_cols; ++i)
        if (m_counts[i] > 0) filled++;

    LOG_INFO("Noise map: %dx%d grid, %d cells with data, default=%.0f dBm",
             m_rows, m_cols, filled, m_default);
}

float NoiseMap::sample(double lat, double lon) const {
    if (m_rows == 0 || m_cols == 0) return m_default;

    // Out of bounds
    if (lat < m_bounds.min_lat || lat > m_bounds.max_lat ||
        lon < m_bounds.min_lon || lon > m_bounds.max_lon)
        return m_default;

    // Bilinear interpolation
    double fr = (m_bounds.max_lat - lat) / m_resolution;
    double fc = (lon - m_bounds.min_lon) / m_resolution;
    int r = std::clamp(static_cast<int>(fr), 0, m_rows - 2);
    int c = std::clamp(static_cast<int>(fc), 0, m_cols - 2);
    float wr = static_cast<float>(fr - r);
    float wc = static_cast<float>(fc - c);

    // Gather the 4 corners, substituting default for no-data cells
    auto val = [&](int rr, int cc) -> float {
        int idx = rr * m_cols + cc;
        return m_counts[idx] > 0 ? m_grid[idx] : m_default;
    };

    float v00 = val(r, c);
    float v01 = val(r, c + 1);
    float v10 = val(r + 1, c);
    float v11 = val(r + 1, c + 1);

    return v00 * (1 - wr) * (1 - wc)
         + v01 * (1 - wr) * wc
         + v10 * wr * (1 - wc)
         + v11 * wr * wc;
}

std::vector<NoiseObservation> load_noise_observations(const std::string& source) {
    std::string body = fetch_or_read(source);
    if (body.empty()) return {};

    std::vector<NoiseObservation> obs;
    try {
        auto json = nlohmann::json::parse(body);
        for (const auto& j : json) {
            if (!j.contains("noisefloor") || j["noisefloor"].is_null()) continue;

            float nf;
            if (j["noisefloor"].is_string()) {
                std::string s = j["noisefloor"];
                if (s.empty()) continue;
                nf = std::stof(s);
            } else {
                nf = j["noisefloor"].get<float>();
            }

            double lat = j.value("lat", 0.0);
            double lon = j.value("lon", 0.0);
            if (lat == 0.0 && lon == 0.0) continue;

            obs.push_back({lat, lon, nf});
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Noise data parse error: %s", e.what());
    }

    LOG_INFO("Loaded %zu noise observations", obs.size());
    return obs;
}

} // namespace meshtile
