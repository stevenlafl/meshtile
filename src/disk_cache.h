#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace meshtile {

class DiskCache {
public:
    explicit DiskCache(const std::string& cache_dir = "");

    bool has(const std::string& key) const;
    std::vector<uint8_t> read(const std::string& key) const;
    bool write(const std::string& key, const std::vector<uint8_t>& data);
    bool remove(const std::string& key);

    const std::string& cache_dir() const { return m_cache_dir; }

private:
    std::string m_cache_dir;
    std::string key_to_path(const std::string& key) const;
    bool ensure_dir(const std::string& path) const;
};

} // namespace meshtile
