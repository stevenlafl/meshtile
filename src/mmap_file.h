#pragma once
#include <cstddef>
#include <string>

namespace meshtile {

class MmapFile {
public:
    MmapFile() = default;
    ~MmapFile();

    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;

    MmapFile(MmapFile&& other) noexcept;
    MmapFile& operator=(MmapFile&& other) noexcept;

    // Open and mmap a file region read-only.
    // offset: byte offset into the file where the mapped region starts.
    // length: bytes to map (0 = from offset to end of file).
    bool open(const std::string& path, size_t offset = 0, size_t length = 0);

    void close();

    bool is_open() const { return m_data != nullptr; }
    const void* data() const { return m_data; }
    size_t size() const { return m_size; }

    template<typename T>
    const T* as() const { return static_cast<const T*>(m_data); }

private:
    void*  m_data      = nullptr;
    size_t m_size      = 0;
    int    m_fd        = -1;
    void*  m_mmap_base = nullptr;
    size_t m_mmap_size = 0;
};

} // namespace meshtile
