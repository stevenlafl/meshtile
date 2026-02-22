#pragma once
#include "disk_cache.h"
#include "types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace meshtile {

class HgtProvider {
public:
    HgtProvider();

    // Load elevation for a 1-degree tile. Returns empty if unavailable.
    std::vector<float> load(int lat_floor, int lon_floor, int& rows, int& cols);

    static Bounds hgt_bounds(int lat_floor, int lon_floor);
    static std::string make_filename(int lat_floor, int lon_floor);

private:
    DiskCache m_cache;

    std::vector<float> read_hgt(const std::vector<uint8_t>& data, int& rows, int& cols);
    std::vector<uint8_t> acquire_hgt(const std::string& filename);
    std::vector<uint8_t> download_hgt(const std::string& filename);
    static std::vector<uint8_t> decompress_gz(const std::vector<uint8_t>& compressed);
};

} // namespace meshtile
