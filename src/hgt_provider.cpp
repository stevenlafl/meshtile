#include "hgt_provider.h"
#include "log.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <curl/curl.h>
#include <zlib.h>

namespace meshtile {

HgtProvider::HgtProvider()
    : m_cache([] {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.cache/mesh3d/hgt";
        return std::string("/tmp/mesh3d/hgt");
    }())
{
    LOG_INFO("HGT cache: %s", m_cache.cache_dir().c_str());
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

std::vector<float> HgtProvider::load(int lat_floor, int lon_floor, int& rows, int& cols) {
    std::string filename = make_filename(lat_floor, lon_floor);
    auto raw = acquire_hgt(filename);
    if (raw.empty()) return {};
    auto elev = read_hgt(raw, rows, cols);
    if (!elev.empty())
        LOG_INFO("HGT: loaded %s (%dx%d)", filename.c_str(), rows, cols);
    return elev;
}

std::vector<float> HgtProvider::read_hgt(const std::vector<uint8_t>& data, int& rows, int& cols) {
    size_t samples = data.size() / 2;
    if (samples == 3601 * 3601) {
        rows = cols = 3601;
    } else if (samples == 1201 * 1201) {
        rows = cols = 1201;
    } else {
        LOG_WARN("HGT: unexpected size %zu bytes (%zu samples)", data.size(), samples);
        return {};
    }

    std::vector<float> elev(samples);
    const uint8_t* p = data.data();
    for (size_t i = 0; i < samples; ++i) {
        int16_t val = static_cast<int16_t>((p[i * 2] << 8) | p[i * 2 + 1]);
        if (val < -1000) val = 0;
        elev[i] = static_cast<float>(val);
    }
    return elev;
}

std::vector<uint8_t> HgtProvider::acquire_hgt(const std::string& filename) {
    if (m_cache.has(filename)) {
        return m_cache.read(filename);
    }

    auto compressed = download_hgt(filename);
    if (compressed.empty()) return {};

    auto raw = decompress_gz(compressed);
    if (raw.empty()) {
        LOG_WARN("HGT: decompression failed for %s", filename.c_str());
        return {};
    }

    m_cache.write(filename, raw);
    LOG_INFO("HGT: cached %s (%zu bytes)", filename.c_str(), raw.size());
    return raw;
}

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
    size_t total = size * nmemb;
    buf->insert(buf->end(),
                static_cast<uint8_t*>(ptr),
                static_cast<uint8_t*>(ptr) + total);
    return total;
}

std::vector<uint8_t> HgtProvider::download_hgt(const std::string& filename) {
    std::string lat_dir = filename.substr(0, 3);
    std::string url = "https://s3.amazonaws.com/elevation-tiles-prod/skadi/"
                    + lat_dir + "/" + filename + ".gz";

    LOG_INFO("HGT: downloading %s", url.c_str());

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::vector<uint8_t> buffer;
    buffer.reserve(3 * 1024 * 1024);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_WARN("HGT: download failed: %s", curl_easy_strerror(res));
        return {};
    }
    if (http_code != 200) {
        LOG_WARN("HGT: HTTP %ld for %s", http_code, url.c_str());
        return {};
    }

    LOG_INFO("HGT: downloaded %zu bytes", buffer.size());
    return buffer;
}

std::vector<uint8_t> HgtProvider::decompress_gz(const std::vector<uint8_t>& compressed) {
    if (compressed.size() < 10) return {};

    z_stream strm{};
    if (inflateInit2(&strm, 15 + 16) != Z_OK) return {};

    strm.next_in = const_cast<uint8_t*>(compressed.data());
    strm.avail_in = static_cast<uInt>(compressed.size());

    std::vector<uint8_t> output(30 * 1024 * 1024); // 30MB max
    strm.next_out = output.data();
    strm.avail_out = static_cast<uInt>(output.size());

    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        LOG_WARN("HGT: inflate failed (ret=%d)", ret);
        return {};
    }

    output.resize(output.size() - strm.avail_out);
    return output;
}

} // namespace meshtile
