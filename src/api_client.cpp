#include "api_client.h"
#include "log.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <fstream>
#include <sstream>

namespace meshtile {

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

static bool is_url(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

static std::string fetch_url(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return {};
    }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        LOG_ERROR("Fetch failed: %s (HTTP %ld)", curl_easy_strerror(res), http_code);
        return {};
    }
    return body;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        LOG_ERROR("Cannot open file: %s", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<Node> fetch_nodes(const ApiClientConfig& config) {
    std::string body;
    if (is_url(config.source)) {
        LOG_INFO("Fetching nodes from %s", config.source.c_str());
        body = fetch_url(config.source);
    } else {
        LOG_INFO("Loading nodes from %s", config.source.c_str());
        body = read_file(config.source);
    }

    if (body.empty()) return {};

    std::vector<Node> nodes;
    try {
        auto json = nlohmann::json::parse(body);
        for (const auto& j : json) {
            double lat = j.value("lat", 0.0);
            double lon = j.value("lon", 0.0);
            int enabled = j.value("enabled", 0);

            if (enabled != 1 || (lat == 0.0 && lon == 0.0)) continue;

            Node n;
            n.id   = j.value("id", "");
            n.name = j.value("name", "");
            n.lat  = lat;
            n.lon  = lon;
            n.tx_power_dbm     = config.default_tx_power_dbm;
            n.antenna_gain_dbi = config.default_antenna_gain_dbi;
            n.frequency_mhz    = config.default_frequency_mhz;
            n.max_range_km     = config.default_max_range_km;
            nodes.push_back(std::move(n));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("JSON parse error: %s", e.what());
    }

    return nodes;
}

} // namespace meshtile
