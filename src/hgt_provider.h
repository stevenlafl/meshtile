#pragma once
#include "mmap_file.h"
#include "types.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace meshtile {

class HgtProvider {
public:
    HgtProvider();

    // Load elevation for a 1-degree tile. Returns nullptr if unavailable.
    // The pointer remains valid for the lifetime of this HgtProvider.
    const float* load(int lat_floor, int lon_floor, int& rows, int& cols);

    static Bounds hgt_bounds(int lat_floor, int lon_floor);
    static std::string make_filename(int lat_floor, int lon_floor);

private:
    std::string m_cache_dir;
    std::unordered_map<std::string, MmapFile> m_mmap_cache;

    // Ensure the .dat float file exists; returns path or empty on failure.
    std::string acquire_dat(const std::string& filename);

    // Stream-download .gz to disk, return true on success.
    bool download_hgt_gz(const std::string& filename, const std::string& gz_path);

    // Decompress .gz file to .hgt file via streaming (64KB buffer).
    static bool decompress_gz_file(const std::string& gz_path, const std::string& out_path);

    // Convert big-endian int16 .hgt to native float .dat via streaming.
    static bool convert_hgt_to_float(const std::string& hgt_path, const std::string& dat_path);
};

} // namespace meshtile
