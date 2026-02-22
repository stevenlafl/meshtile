#include "disk_cache.h"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace meshtile {

DiskCache::DiskCache(const std::string& cache_dir) {
    if (!cache_dir.empty()) {
        m_cache_dir = cache_dir;
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            m_cache_dir = std::string(home) + "/.cache/mesh3d/hgt";
        } else {
            m_cache_dir = "/tmp/mesh3d/hgt";
        }
    }
}

std::string DiskCache::key_to_path(const std::string& key) const {
    return m_cache_dir + "/" + key;
}

bool DiskCache::ensure_dir(const std::string& path) const {
    fs::path p(path);
    fs::path dir = p.parent_path();
    if (dir.empty()) return true;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec;
}

bool DiskCache::has(const std::string& key) const {
    return fs::exists(key_to_path(key));
}

std::vector<uint8_t> DiskCache::read(const std::string& key) const {
    std::string path = key_to_path(key);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool DiskCache::write(const std::string& key, const std::vector<uint8_t>& data) {
    std::string path = key_to_path(key);
    if (!ensure_dir(path)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool DiskCache::remove(const std::string& key) {
    std::error_code ec;
    return fs::remove(key_to_path(key), ec);
}

} // namespace meshtile
