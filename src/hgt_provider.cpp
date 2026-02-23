#include "hgt_provider.h"
#include "http_fetch.h"
#include "log.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <zlib.h>

namespace fs = std::filesystem;

namespace meshtile {

HgtProvider::HgtProvider() {
    const char* home = std::getenv("HOME");
    m_cache_dir = home ? std::string(home) + "/.cache/mesh3d/hgt"
                       : std::string("/tmp/mesh3d/hgt");
    fs::create_directories(m_cache_dir);
    LOG_INFO("HGT cache: %s", m_cache_dir.c_str());
}

std::string HgtProvider::make_filename(int lat_floor, int lon_floor) {
    char buf[32];
    char ns = lat_floor >= 0 ? 'N' : 'S';
    char ew = lon_floor >= 0 ? 'E' : 'W';
    std::snprintf(buf, sizeof(buf), "%c%02d%c%03d.hgt",
                  ns, std::abs(lat_floor), ew, std::abs(lon_floor));
    return buf;
}

Bounds HgtProvider::hgt_bounds(int lat_floor, int lon_floor) {
    return {
        static_cast<double>(lat_floor),
        static_cast<double>(lat_floor + 1),
        static_cast<double>(lon_floor),
        static_cast<double>(lon_floor + 1)
    };
}

const float* HgtProvider::load(int lat_floor, int lon_floor, int& rows, int& cols) {
    std::string filename = make_filename(lat_floor, lon_floor);

    // Check if already mmapped
    auto it = m_mmap_cache.find(filename);
    if (it != m_mmap_cache.end()) {
        size_t samples = it->second.size() / sizeof(float);
        if (samples == 3601UL * 3601) { rows = cols = 3601; }
        else if (samples == 1201UL * 1201) { rows = cols = 1201; }
        else return nullptr;
        return it->second.as<float>();
    }

    // Ensure .dat file exists on disk
    std::string dat_path = acquire_dat(filename);
    if (dat_path.empty()) return nullptr;

    // mmap the .dat file
    MmapFile mf;
    if (!mf.open(dat_path)) return nullptr;

    size_t samples = mf.size() / sizeof(float);
    if (samples == 3601UL * 3601) { rows = cols = 3601; }
    else if (samples == 1201UL * 1201) { rows = cols = 1201; }
    else {
        LOG_WARN("HGT: unexpected .dat size %zu bytes for %s", mf.size(), filename.c_str());
        return nullptr;
    }

    const float* ptr = mf.as<float>();
    m_mmap_cache.emplace(filename, std::move(mf));

    LOG_INFO("HGT: mmapped %s (%dx%d)", filename.c_str(), rows, cols);
    return ptr;
}

std::string HgtProvider::acquire_dat(const std::string& filename) {
    // filename is e.g. "N39W105.hgt"
    std::string base = filename.substr(0, filename.size() - 4); // "N39W105"
    std::string dat_path = m_cache_dir + "/" + base + ".dat";
    std::string hgt_path = m_cache_dir + "/" + filename;

    // Already have the .dat float file?
    if (fs::exists(dat_path)) return dat_path;

    // Have the raw .hgt but no .dat? Just convert.
    if (fs::exists(hgt_path)) {
        LOG_INFO("HGT: converting %s -> .dat", filename.c_str());
        if (convert_hgt_to_float(hgt_path, dat_path)) return dat_path;
        LOG_WARN("HGT: conversion failed for %s", filename.c_str());
        return {};
    }

    // Need to download
    std::string gz_path = m_cache_dir + "/" + filename + ".gz";
    if (!download_hgt_gz(filename, gz_path)) return {};

    // Decompress .gz -> .hgt (streaming, 64KB buffer)
    LOG_INFO("HGT: decompressing %s", filename.c_str());
    if (!decompress_gz_file(gz_path, hgt_path)) {
        LOG_WARN("HGT: decompression failed for %s", filename.c_str());
        fs::remove(gz_path);
        return {};
    }
    fs::remove(gz_path); // clean up .gz

    // Convert .hgt -> .dat (streaming, ~48KB buffer)
    LOG_INFO("HGT: converting %s -> .dat", filename.c_str());
    if (!convert_hgt_to_float(hgt_path, dat_path)) {
        LOG_WARN("HGT: conversion failed for %s", filename.c_str());
        return {};
    }

    LOG_INFO("HGT: cached %s", dat_path.c_str());
    return dat_path;
}

bool HgtProvider::download_hgt_gz(const std::string& filename, const std::string& gz_path) {
    std::string lat_dir = filename.substr(0, 3);
    std::string url = "https://s3.amazonaws.com/elevation-tiles-prod/skadi/"
                    + lat_dir + "/" + filename + ".gz";
    LOG_INFO("HGT: downloading %s", url.c_str());
    return fetch_to_file(url, gz_path);
}

bool HgtProvider::decompress_gz_file(const std::string& gz_path, const std::string& out_path) {
    gzFile gz = gzopen(gz_path.c_str(), "rb");
    if (!gz) return false;

    FILE* out = fopen(out_path.c_str(), "wb");
    if (!out) { gzclose(gz); return false; }

    uint8_t buf[64 * 1024]; // 64KB stack buffer
    int n;
    while ((n = gzread(gz, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
            fclose(out);
            gzclose(gz);
            return false;
        }
    }

    fclose(out);
    gzclose(gz);
    return n == 0; // 0 = EOF, -1 = error
}

bool HgtProvider::convert_hgt_to_float(const std::string& hgt_path, const std::string& dat_path) {
    std::ifstream f(hgt_path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    size_t file_size = static_cast<size_t>(f.tellg());
    size_t samples = file_size / 2;

    if (samples != 3601UL * 3601 && samples != 1201UL * 1201) {
        LOG_WARN("HGT: unexpected size %zu bytes for %s", file_size, hgt_path.c_str());
        return false;
    }

    f.seekg(0);
    FILE* out = fopen(dat_path.c_str(), "wb");
    if (!out) return false;

    constexpr size_t CHUNK = 8192;
    uint8_t in_buf[CHUNK * 2];
    float out_buf[CHUNK];

    size_t remaining = samples;
    while (remaining > 0) {
        size_t batch = std::min(remaining, CHUNK);
        f.read(reinterpret_cast<char*>(in_buf), static_cast<std::streamsize>(batch * 2));

        for (size_t i = 0; i < batch; ++i) {
            int16_t val = static_cast<int16_t>((in_buf[i * 2] << 8) | in_buf[i * 2 + 1]);
            if (val < -1000) val = 0;
            out_buf[i] = static_cast<float>(val);
        }

        fwrite(out_buf, sizeof(float), batch, out);
        remaining -= batch;
    }

    fclose(out);
    return true;
}

} // namespace meshtile
