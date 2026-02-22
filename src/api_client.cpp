#include "api_client.h"
#include "http_fetch.h"
#include "log.h"
#include <nlohmann/json.hpp>

namespace meshtile {

std::vector<Node> fetch_nodes(const ApiClientConfig& config) {
    std::string body = fetch_or_read(config.source);

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
