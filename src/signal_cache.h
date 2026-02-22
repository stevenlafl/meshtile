#pragma once
#include "types.h"
#include "hgt_provider.h"
#include "disk_cache.h"
#include <vector>
#include <unordered_set>
#include <mutex>
#include <string>

namespace meshtile {

class SignalCache {
public:
    SignalCache();

    // Precompute signal grids. Loads cached grids from disk when available.
    bool precompute(const std::vector<Node>& nodes, const RfConfig& rf_config);

    float sample(double lat, double lon) const;
    std::vector<size_t> grids_overlapping(const Bounds& bounds) const;
    float sample_grids(double lat, double lon,
                       const std::vector<size_t>& grid_indices) const;

    Bounds coverage_bounds() const;
    size_t grid_count() const { return m_grids.size(); }

    // Sample a specific node's grid at a location. Returns -999 if no data.
    float sample_node(const std::string& node_id, double lat, double lon) const;

private:
    std::vector<SignalGrid> m_grids;
    HgtProvider m_hgt;
    std::mutex m_hgt_mutex;
    DiskCache m_grid_cache;  // per-node grid files: ~/.cache/meshtile/grids/
    std::string m_cache_dir;

    std::unordered_set<std::string> load_known_nodes();
    void save_known_nodes(const std::unordered_set<std::string>& ids);

    SignalGrid compute_node(const Node& node, const RfConfig& rf_config);
    bool save_grid(const SignalGrid& grid, uint64_t params_hash);
    bool load_grid(const std::string& node_id, uint64_t expected_hash, SignalGrid& grid);
};

} // namespace meshtile
