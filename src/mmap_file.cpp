#include "mmap_file.h"
#include "log.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace meshtile {

MmapFile::~MmapFile() { close(); }

MmapFile::MmapFile(MmapFile&& other) noexcept
    : m_data(other.m_data), m_size(other.m_size), m_fd(other.m_fd),
      m_mmap_base(other.m_mmap_base), m_mmap_size(other.m_mmap_size) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_fd = -1;
    other.m_mmap_base = nullptr;
    other.m_mmap_size = 0;
}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
    if (this != &other) {
        close();
        m_data = other.m_data;
        m_size = other.m_size;
        m_fd = other.m_fd;
        m_mmap_base = other.m_mmap_base;
        m_mmap_size = other.m_mmap_size;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_fd = -1;
        other.m_mmap_base = nullptr;
        other.m_mmap_size = 0;
    }
    return *this;
}

bool MmapFile::open(const std::string& path, size_t offset, size_t length) {
    close();

    m_fd = ::open(path.c_str(), O_RDONLY);
    if (m_fd < 0) {
        LOG_ERROR("MmapFile: cannot open %s", path.c_str());
        return false;
    }

    struct stat st;
    if (fstat(m_fd, &st) < 0) {
        LOG_ERROR("MmapFile: fstat failed for %s", path.c_str());
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    size_t file_size = static_cast<size_t>(st.st_size);
    if (offset >= file_size) {
        LOG_ERROR("MmapFile: offset %zu >= file size %zu for %s",
                  offset, file_size, path.c_str());
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    if (length == 0)
        length = file_size - offset;

    if (offset + length > file_size) {
        LOG_ERROR("MmapFile: offset+length %zu > file size %zu for %s",
                  offset + length, file_size, path.c_str());
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    size_t aligned_offset = (offset / page_size) * page_size;
    size_t offset_delta = offset - aligned_offset;

    m_mmap_size = length + offset_delta;
    m_mmap_base = mmap(nullptr, m_mmap_size, PROT_READ, MAP_PRIVATE,
                       m_fd, static_cast<off_t>(aligned_offset));

    if (m_mmap_base == MAP_FAILED) {
        LOG_ERROR("MmapFile: mmap failed for %s", path.c_str());
        m_mmap_base = nullptr;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_data = static_cast<char*>(m_mmap_base) + offset_delta;
    m_size = length;
    return true;
}

void MmapFile::close() {
    if (m_mmap_base) {
        munmap(m_mmap_base, m_mmap_size);
        m_mmap_base = nullptr;
        m_data = nullptr;
        m_size = 0;
        m_mmap_size = 0;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace meshtile
