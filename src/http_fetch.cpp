#include "http_fetch.h"
#include "log.h"
#include <curl/curl.h>
#include <fstream>
#include <sstream>

namespace meshtile {

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* str = static_cast<std::string*>(userdata);
    str->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool is_url(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

std::string fetch_url(const std::string& url) {
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

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        LOG_ERROR("Cannot open file: %s", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string fetch_or_read(const std::string& source) {
    if (is_url(source)) {
        LOG_INFO("Fetching from %s", source.c_str());
        return fetch_url(source);
    } else {
        LOG_INFO("Loading from %s", source.c_str());
        return read_file(source);
    }
}

} // namespace meshtile
