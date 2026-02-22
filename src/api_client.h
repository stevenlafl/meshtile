#pragma once
#include "types.h"
#include <vector>
#include <string>

namespace meshtile {

struct ApiClientConfig {
    // URL or local file path. If it starts with http:// or https://, fetched via curl.
    std::string source = "https://den.meshmapper.net/api.php?request=repeaters";
    float default_tx_power_dbm     = 22.0f;
    float default_antenna_gain_dbi = 2.0f;
    float default_frequency_mhz    = 906.875f;
    float default_max_range_km     = 30.0f;
};

std::vector<Node> fetch_nodes(const ApiClientConfig& config = {});

} // namespace meshtile
