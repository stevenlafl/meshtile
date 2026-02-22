#include "signal_cache.h"
#include "log.h"
#include <itm.h>
#include <cmath>
#include <algorithm>
#include <future>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;

namespace meshtile {

static constexpr float NO_SIGNAL = -999.0f;

// FNV-1a hash of all parameters that affect grid computation.
// If any of these change, the cached grid is stale.
static uint64_t compute_params_hash(const Node& n, const RfConfig& rf) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    auto mix = [&](const void* data, size_t len) {
        auto p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL; // FNV prime
        }
    };
    mix(&n.lat, sizeof(n.lat));
    mix(&n.lon, sizeof(n.lon));
    mix(&n.max_range_km, sizeof(n.max_range_km));
    mix(&n.tx_power_dbm, sizeof(n.tx_power_dbm));
    mix(&n.antenna_gain_dbi, sizeof(n.antenna_gain_dbi));
    mix(&n.cable_loss_db, sizeof(n.cable_loss_db));
    mix(&n.antenna_height_m, sizeof(n.antenna_height_m));
    mix(&n.frequency_mhz, sizeof(n.frequency_mhz));
    mix(&n.rx_sensitivity_dbm, sizeof(n.rx_sensitivity_dbm));
    mix(&rf.rx_sensitivity_dbm, sizeof(rf.rx_sensitivity_dbm));
    mix(&rf.rx_height_agl_m, sizeof(rf.rx_height_agl_m));
    mix(&rf.rx_antenna_gain_dbi, sizeof(rf.rx_antenna_gain_dbi));
    mix(&rf.rx_cable_loss_db, sizeof(rf.rx_cable_loss_db));
    mix(&rf.climate, sizeof(rf.climate));
    mix(&rf.refractivity, sizeof(rf.refractivity));
    mix(&rf.polarization, sizeof(rf.polarization));
    mix(&rf.ground_dielectric, sizeof(rf.ground_dielectric));
    mix(&rf.ground_conductivity, sizeof(rf.ground_conductivity));
    mix(&rf.clutter_height_m, sizeof(rf.clutter_height_m));
    mix(&rf.time_pct, sizeof(rf.time_pct));
    mix(&rf.location_pct, sizeof(rf.location_pct));
    mix(&rf.situation_pct, sizeof(rf.situation_pct));
    return h;
}

// Extract terrain profile between two grid cells (adapted from mesh3d itm.cpp)
static std::vector<double> extract_profile(
    const float* elevation, int rows, int cols,
    int r0, int c0, int r1, int c1,
    float cell_meters, int max_samples, double& out_step_m)
{
    int dr = r1 - r0;
    int dc = c1 - c0;
    float dist_cells = std::sqrt(static_cast<float>(dr * dr + dc * dc));
    int n_samples = static_cast<int>(dist_cells) + 1;

    if (n_samples < 2) {
        out_step_m = cell_meters;
        return {static_cast<double>(elevation[r0 * cols + c0]),
                static_cast<double>(elevation[r1 * cols + c1])};
    }

    int step = 1;
    if (n_samples > max_samples) {
        step = (n_samples + max_samples - 1) / max_samples;
        n_samples = (n_samples + step - 1) / step;
    }

    std::vector<double> profile;
    profile.reserve(n_samples);

    for (int i = 0; i < n_samples; ++i) {
        float t = static_cast<float>(i * step) / dist_cells;
        t = std::min(t, 1.0f);
        float fr = r0 + dr * t;
        float fc = c0 + dc * t;

        // Bilinear interpolation to avoid axis-aligned artifacts
        int ir = std::clamp(static_cast<int>(fr), 0, rows - 2);
        int ic = std::clamp(static_cast<int>(fc), 0, cols - 2);
        float wr = fr - ir;
        float wc = fc - ic;
        double e00 = elevation[ir * cols + ic];
        double e01 = elevation[ir * cols + ic + 1];
        double e10 = elevation[(ir + 1) * cols + ic];
        double e11 = elevation[(ir + 1) * cols + ic + 1];
        double elev = e00 * (1 - wr) * (1 - wc)
                    + e01 * (1 - wr) * wc
                    + e10 * wr * (1 - wc)
                    + e11 * wr * wc;
        profile.push_back(elev);
    }

    if (!profile.empty()) {
        profile.back() = static_cast<double>(
            elevation[std::clamp(r1, 0, rows - 1) * cols +
                      std::clamp(c1, 0, cols - 1)]);
    }

    out_step_m = static_cast<double>(cell_meters) * step;
    return profile;
}

// ── Constructor ──

SignalCache::SignalCache()
    : m_grid_cache([]{
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.cache/meshtile/grids";
        return std::string("/tmp/meshtile/grids");
    }())
{
    m_cache_dir = m_grid_cache.cache_dir();
    auto pos = m_cache_dir.rfind("/grids");
    if (pos != std::string::npos)
        m_cache_dir = m_cache_dir.substr(0, pos);
    else
        m_cache_dir += "/..";
    LOG_INFO("Grid cache: %s", m_grid_cache.cache_dir().c_str());
}

// ── Per-node grid serialization ──

bool SignalCache::save_grid(const SignalGrid& grid, uint64_t params_hash) {
    // Binary format: [hash:8][node_id_len:4][node_id][bounds:32][rows:4][cols:4][signal:rows*cols*4]
    std::string key = grid.node_id + ".bin";
    size_t data_size = 8 + 4 + grid.node_id.size() + sizeof(Bounds) + 8 +
                       grid.rows * grid.cols * sizeof(float);
    std::vector<uint8_t> buf(data_size);
    uint8_t* p = buf.data();

    std::memcpy(p, &params_hash, 8); p += 8;
    uint32_t id_len = static_cast<uint32_t>(grid.node_id.size());
    std::memcpy(p, &id_len, 4); p += 4;
    std::memcpy(p, grid.node_id.data(), id_len); p += id_len;
    std::memcpy(p, &grid.bounds, sizeof(Bounds)); p += sizeof(Bounds);
    int32_t rows = grid.rows, cols = grid.cols;
    std::memcpy(p, &rows, 4); p += 4;
    std::memcpy(p, &cols, 4); p += 4;
    std::memcpy(p, grid.signal.data(), grid.signal.size() * sizeof(float));

    return m_grid_cache.write(key, buf);
}

bool SignalCache::load_grid(const std::string& node_id, uint64_t expected_hash,
                            SignalGrid& grid) {
    std::string key = node_id + ".bin";
    if (!m_grid_cache.has(key)) return false;

    auto buf = m_grid_cache.read(key);
    if (buf.size() < 20) return false;  // at least hash + id_len + some data

    const uint8_t* p = buf.data();

    uint64_t stored_hash;
    std::memcpy(&stored_hash, p, 8); p += 8;
    if (stored_hash != expected_hash) {
        LOG_INFO("  Grid %s: params changed, will recompute", node_id.c_str());
        return false;
    }

    uint32_t id_len;
    std::memcpy(&id_len, p, 4); p += 4;
    if (8 + 4 + id_len + sizeof(Bounds) + 8 > buf.size()) return false;

    grid.node_id = std::string(reinterpret_cast<const char*>(p), id_len); p += id_len;
    std::memcpy(&grid.bounds, p, sizeof(Bounds)); p += sizeof(Bounds);

    int32_t rows, cols;
    std::memcpy(&rows, p, 4); p += 4;
    std::memcpy(&cols, p, 4); p += 4;
    grid.rows = rows;
    grid.cols = cols;

    size_t expected = static_cast<size_t>(rows) * cols * sizeof(float);
    size_t remaining = buf.size() - static_cast<size_t>(p - buf.data());
    if (remaining < expected) return false;

    grid.signal.resize(rows * cols);
    std::memcpy(grid.signal.data(), p, expected);
    return true;
}

// ── Known nodes persistence ──

std::unordered_set<std::string> SignalCache::load_known_nodes() {
    std::string path = m_cache_dir + "/known_nodes.txt";
    std::unordered_set<std::string> ids;
    std::ifstream f(path);
    if (!f) return ids;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) ids.insert(line);
    }
    return ids;
}

void SignalCache::save_known_nodes(const std::unordered_set<std::string>& ids) {
    std::string path = m_cache_dir + "/known_nodes.txt";
    fs::create_directories(m_cache_dir);
    std::ofstream f(path);
    for (const auto& id : ids) f << id << "\n";
}

// ── Compute single node ──

SignalGrid SignalCache::compute_node(const Node& node, const RfConfig& rf_config) {
    SignalGrid grid;
    grid.node_id = node.id;

    double lat_extent = node.max_range_km / 111.32;
    double lon_extent = node.max_range_km /
        (111.32 * std::cos(node.lat * M_PI / 180.0));

    Bounds cov;
    cov.min_lat = node.lat - lat_extent;
    cov.max_lat = node.lat + lat_extent;
    cov.min_lon = node.lon - lon_extent;
    cov.max_lon = node.lon + lon_extent;

    int lat_min = static_cast<int>(std::floor(cov.min_lat));
    int lat_max = static_cast<int>(std::floor(cov.max_lat));
    int lon_min = static_cast<int>(std::floor(cov.min_lon));
    int lon_max = static_cast<int>(std::floor(cov.max_lon));

    int n_lat_tiles = lat_max - lat_min + 1;
    int n_lon_tiles = lon_max - lon_min + 1;
    int samples_per_deg = 1200;
    int total_rows = n_lat_tiles * samples_per_deg + 1;
    int total_cols = n_lon_tiles * samples_per_deg + 1;

    std::vector<float> elevation(total_rows * total_cols, 0.0f);

    for (int lt = lat_min; lt <= lat_max; ++lt) {
        for (int ln = lon_min; ln <= lon_max; ++ln) {
            int tile_rows = 0, tile_cols = 0;
            std::vector<float> tile;
            {
                std::lock_guard<std::mutex> lock(m_hgt_mutex);
                tile = m_hgt.load(lt, ln, tile_rows, tile_cols);
            }
            if (tile.empty()) continue;

            int actual_samp = tile_rows - 1;
            int ratio = 1;
            if (actual_samp == 3600) ratio = 3; // SRTM1 -> subsample

            for (int r = 0; r <= samples_per_deg; ++r) {
                for (int c = 0; c <= samples_per_deg; ++c) {
                    int src_r = std::min(r * ratio, tile_rows - 1);
                    int src_c = std::min(c * ratio, tile_cols - 1);
                    int dst_r = (lat_max - lt) * samples_per_deg + r;
                    int dst_c = (ln - lon_min) * samples_per_deg + c;
                    if (dst_r >= 0 && dst_r < total_rows &&
                        dst_c >= 0 && dst_c < total_cols) {
                        elevation[dst_r * total_cols + dst_c] =
                            tile[src_r * tile_cols + src_c];
                    }
                }
            }
        }
    }

    Bounds elev_bounds;
    elev_bounds.min_lat = lat_min;
    elev_bounds.max_lat = lat_max + 1;
    elev_bounds.min_lon = lon_min;
    elev_bounds.max_lon = lon_max + 1;

    grid.bounds = elev_bounds;
    grid.rows = total_rows;
    grid.cols = total_cols;
    grid.signal.assign(total_rows * total_cols, NO_SIGNAL);

    double lat_res = (elev_bounds.max_lat - elev_bounds.min_lat) / (total_rows - 1);
    double lon_res = (elev_bounds.max_lon - elev_bounds.min_lon) / (total_cols - 1);

    int nr = static_cast<int>((elev_bounds.max_lat - node.lat) / lat_res);
    int nc = static_cast<int>((node.lon - elev_bounds.min_lon) / lon_res);
    nr = std::clamp(nr, 0, total_rows - 1);
    nc = std::clamp(nc, 0, total_cols - 1);

    double center_lat = node.lat;
    double m_per_deg_lat = 111320.0;
    double m_per_deg_lon = 111320.0 * std::cos(center_lat * M_PI / 180.0);
    float cell_m_lat = static_cast<float>(lat_res * m_per_deg_lat);
    float cell_m_lon = static_cast<float>(lon_res * m_per_deg_lon);
    float cell_m = (cell_m_lat + cell_m_lon) * 0.5f;

    float eirp = node.tx_power_dbm + node.antenna_gain_dbi - node.cable_loss_db;
    float max_range_cells = node.max_range_km * 1000.0f / cell_m;

    int climate = rf_config.climate;
    double N_0 = rf_config.refractivity;
    int pol = rf_config.polarization;
    double epsilon = rf_config.ground_dielectric;
    double sigma = rf_config.ground_conductivity;
    int mdvar = 12;
    double time_pct = rf_config.time_pct;
    double location_pct = rf_config.location_pct;
    double situation_pct = rf_config.situation_pct;
    float clutter_h = rf_config.clutter_height_m;

    for (int r = 0; r < total_rows; ++r) {
        for (int c = 0; c < total_cols; ++c) {
            int dr = r - nr;
            int dc = c - nc;
            float dist_cells = std::sqrt(static_cast<float>(dr * dr + dc * dc));

            if (dist_cells < 0.5f) {
                grid.signal[r * total_cols + c] = eirp;
                continue;
            }
            if (dist_cells > max_range_cells) continue;

            double step_m = 0;
            auto profile = extract_profile(
                elevation.data(), total_rows, total_cols,
                nr, nc, r, c, cell_m, 600, step_m);

            if (profile.size() < 2) continue;

            int n_pts = static_cast<int>(profile.size());
            std::vector<double> pfl(n_pts + 2);
            pfl[0] = static_cast<double>(n_pts - 1);
            pfl[1] = step_m;
            for (int i = 0; i < n_pts; ++i) {
                pfl[i + 2] = profile[i] + static_cast<double>(clutter_h);
            }

            double A_db = 0;
            long warnings = 0;
            int ret = ITM_P2P_TLS(
                static_cast<double>(node.antenna_height_m),
                static_cast<double>(rf_config.rx_height_agl_m),
                pfl.data(),
                climate, N_0,
                static_cast<double>(node.frequency_mhz),
                pol, epsilon, sigma, mdvar,
                time_pct, location_pct, situation_pct,
                &A_db, &warnings);

            if (ret != 0 && ret != 1) continue;

            float received = eirp - static_cast<float>(A_db)
                           + rf_config.rx_antenna_gain_dbi
                           - rf_config.rx_cable_loss_db;

            if (received >= rf_config.rx_sensitivity_dbm) {
                grid.signal[r * total_cols + c] = received;
            }
        }
    }

    return grid;
}

// ── Precompute with caching ──

bool SignalCache::precompute(const std::vector<Node>& nodes, const RfConfig& rf_config) {
    LOG_INFO("Pre-computing signal grids for %zu nodes...", nodes.size());

    auto known = load_known_nodes();
    bool has_new_nodes = false;

    // Determine which nodes need computing vs loading from cache
    std::vector<size_t> to_compute; // indices into nodes
    std::vector<size_t> to_load;    // indices into nodes

    for (size_t i = 0; i < nodes.size(); ++i) {
        uint64_t hash = compute_params_hash(nodes[i], rf_config);
        SignalGrid cached;
        if (load_grid(nodes[i].id, hash, cached)) {
            m_grids.push_back(std::move(cached));
            to_load.push_back(i);
        } else {
            to_compute.push_back(i);
        }
        if (known.find(nodes[i].id) == known.end()) {
            has_new_nodes = true;
        }
    }

    LOG_INFO("  %zu cached, %zu to compute", to_load.size(), to_compute.size());

    // Compute missing grids in parallel
    if (!to_compute.empty()) {
        std::vector<std::future<SignalGrid>> futures;
        for (size_t idx : to_compute) {
            futures.push_back(std::async(std::launch::async,
                [this, &nodes, &rf_config, idx]() {
                    LOG_INFO("  Computing node: %s", nodes[idx].name.c_str());
                    return compute_node(nodes[idx], rf_config);
                }));
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            auto grid = futures[i].get();
            uint64_t hash = compute_params_hash(nodes[to_compute[i]], rf_config);
            save_grid(grid, hash);
            m_grids.push_back(std::move(grid));
        }
    }

    // Update known nodes
    std::unordered_set<std::string> new_known;
    for (const auto& n : nodes) new_known.insert(n.id);
    save_known_nodes(new_known);

    LOG_INFO("Pre-computation complete: %zu grids (%s)",
             m_grids.size(), has_new_nodes ? "new nodes detected" : "all cached");
    return has_new_nodes;
}

// ── Sampling ──

float SignalCache::sample(double lat, double lon) const {
    float best = NO_SIGNAL;
    for (const auto& g : m_grids) {
        if (lat < g.bounds.min_lat || lat > g.bounds.max_lat ||
            lon < g.bounds.min_lon || lon > g.bounds.max_lon)
            continue;

        double row_f = (g.bounds.max_lat - lat) /
                       (g.bounds.max_lat - g.bounds.min_lat) * (g.rows - 1);
        double col_f = (lon - g.bounds.min_lon) /
                       (g.bounds.max_lon - g.bounds.min_lon) * (g.cols - 1);
        int row = std::clamp(static_cast<int>(row_f), 0, g.rows - 1);
        int col = std::clamp(static_cast<int>(col_f), 0, g.cols - 1);

        float val = g.signal[row * g.cols + col];
        if (val > best) best = val;
    }
    return best;
}

std::vector<size_t> SignalCache::grids_overlapping(const Bounds& bounds) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < m_grids.size(); ++i) {
        const auto& g = m_grids[i];
        if (g.bounds.max_lat < bounds.min_lat || g.bounds.min_lat > bounds.max_lat ||
            g.bounds.max_lon < bounds.min_lon || g.bounds.min_lon > bounds.max_lon)
            continue;
        result.push_back(i);
    }
    return result;
}

float SignalCache::sample_grids(double lat, double lon,
                                 const std::vector<size_t>& grid_indices) const {
    float best = NO_SIGNAL;
    for (size_t idx : grid_indices) {
        const auto& g = m_grids[idx];
        if (lat < g.bounds.min_lat || lat > g.bounds.max_lat ||
            lon < g.bounds.min_lon || lon > g.bounds.max_lon)
            continue;

        double row_f = (g.bounds.max_lat - lat) /
                       (g.bounds.max_lat - g.bounds.min_lat) * (g.rows - 1);
        double col_f = (lon - g.bounds.min_lon) /
                       (g.bounds.max_lon - g.bounds.min_lon) * (g.cols - 1);
        int row = std::clamp(static_cast<int>(row_f), 0, g.rows - 1);
        int col = std::clamp(static_cast<int>(col_f), 0, g.cols - 1);

        float val = g.signal[row * g.cols + col];
        if (val > best) best = val;
    }
    return best;
}

float SignalCache::sample_node(const std::string& node_id, double lat, double lon) const {
    for (const auto& g : m_grids) {
        if (g.node_id != node_id) continue;
        if (lat < g.bounds.min_lat || lat > g.bounds.max_lat ||
            lon < g.bounds.min_lon || lon > g.bounds.max_lon)
            return NO_SIGNAL;

        double row_f = (g.bounds.max_lat - lat) /
                       (g.bounds.max_lat - g.bounds.min_lat) * (g.rows - 1);
        double col_f = (lon - g.bounds.min_lon) /
                       (g.bounds.max_lon - g.bounds.min_lon) * (g.cols - 1);
        int row = std::clamp(static_cast<int>(row_f), 0, g.rows - 1);
        int col = std::clamp(static_cast<int>(col_f), 0, g.cols - 1);
        return g.signal[row * g.cols + col];
    }
    return NO_SIGNAL;
}

Bounds SignalCache::coverage_bounds() const {
    if (m_grids.empty()) return {0, 0, 0, 0};
    Bounds b = m_grids[0].bounds;
    for (size_t i = 1; i < m_grids.size(); ++i) {
        b.min_lat = std::min(b.min_lat, m_grids[i].bounds.min_lat);
        b.max_lat = std::max(b.max_lat, m_grids[i].bounds.max_lat);
        b.min_lon = std::min(b.min_lon, m_grids[i].bounds.min_lon);
        b.max_lon = std::max(b.max_lon, m_grids[i].bounds.max_lon);
    }
    return b;
}

} // namespace meshtile
