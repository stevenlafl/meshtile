#include "api_client.h"
#include "signal_cache.h"
#include "log.h"
#include "types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <string>
#include <unordered_map>

struct Observation {
    std::string repeater_id;
    double client_lat, client_lon;
    std::string who;
    float snr;           // dB, from heard_repeats
    float noisefloor;    // dBm
    float observed_rssi; // noisefloor + snr
    float dist_km;
};

static float haversine_km(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371.0;
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    return static_cast<float>(R * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a)));
}

int main(int argc, char* argv[]) {
    meshtile::log_set_level(meshtile::LogLevel::Info);

    std::string nodes_path = "nodes.json";
    std::string map_data_path = "map_data.json";
    float max_range = 30.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--nodes" && i + 1 < argc) nodes_path = argv[++i];
        else if (arg == "--map-data" && i + 1 < argc) map_data_path = argv[++i];
        else if (arg == "--max-range" && i + 1 < argc) max_range = std::atof(argv[++i]);
    }

    // Load repeaters
    meshtile::ApiClientConfig api_config;
    api_config.source = nodes_path;
    api_config.default_max_range_km = max_range;
    auto nodes = meshtile::fetch_nodes(api_config);
    if (nodes.empty()) {
        LOG_ERROR("No nodes loaded");
        return 1;
    }

    // Build repeater lookup
    std::unordered_map<std::string, const meshtile::Node*> rep_map;
    for (const auto& n : nodes) rep_map[n.id] = &n;

    // Precompute signal grids
    meshtile::RfConfig rf_config;
    meshtile::SignalCache cache;
    cache.precompute(nodes, rf_config);

    // Load map_data
    std::ifstream f(map_data_path);
    if (!f) {
        LOG_ERROR("Cannot open %s", map_data_path.c_str());
        return 1;
    }
    auto map_json = nlohmann::json::parse(f);
    LOG_INFO("Loaded %zu map_data entries", map_json.size());

    // Parse observations: extract repeater SNR from heard_repeats field
    // Format: "FA(5.25)[39.9872,-105.0898]" or "FA?(-9.50)[...]"
    std::regex re(R"(([0-9A-Fa-f]{2})\??\((-?[\d.]+)\)\[)");
    std::vector<Observation> observations;

    for (const auto& j : map_json) {
        std::string hr = j.value("heard_repeats", "");
        if (hr.empty() || hr == "None") continue;

        if (!j.contains("noisefloor") || j["noisefloor"].is_null()) continue;
        float noisefloor;
        if (j["noisefloor"].is_string()) {
            std::string nf_str = j["noisefloor"];
            if (nf_str.empty()) continue;
            noisefloor = std::stof(nf_str);
        } else {
            noisefloor = j["noisefloor"].get<float>();
        }

        double clat = j.value("lat", 0.0);
        double clon = j.value("lon", 0.0);
        std::string who = j.value("who", "");

        auto begin = std::sregex_iterator(hr.begin(), hr.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string rid = (*it)[1].str();
            // Uppercase the ID to match repeaters
            for (auto& c : rid) c = std::toupper(c);

            if (rep_map.find(rid) == rep_map.end()) continue;

            float snr = std::stof((*it)[2].str());
            float observed_rssi = noisefloor + snr;
            float dist = haversine_km(rep_map[rid]->lat, rep_map[rid]->lon, clat, clon);

            // Filter: skip observations beyond max_range or within ~200m
            // (near-field: ITM grid cell is ~90m, so sub-cell distances are unreliable)
            if (dist > max_range || dist < 0.2f) continue;

            observations.push_back({rid, clat, clon, who, snr, noisefloor,
                                    observed_rssi, dist});
        }
    }

    LOG_INFO("Parsed %zu usable observations across repeaters", observations.size());

    // Compare ITM predictions vs observed
    struct Result {
        float predicted;
        float observed;
        float error;  // predicted - observed
        float dist_km;
        std::string repeater_id;
        std::string who;
    };
    std::vector<Result> results;

    int no_signal = 0;
    for (const auto& obs : observations) {
        float predicted = cache.sample_node(obs.repeater_id,
                                            obs.client_lat, obs.client_lon);
        if (predicted <= -999.0f) {
            no_signal++;
            continue;
        }

        results.push_back({predicted, obs.observed_rssi,
                           predicted - obs.observed_rssi,
                           obs.dist_km, obs.repeater_id, obs.who});
    }

    LOG_INFO("Results: %zu compared, %d had no ITM signal", results.size(), no_signal);

    if (results.empty()) {
        LOG_ERROR("No comparable results");
        return 1;
    }

    // Statistics
    float sum_err = 0, sum_abs_err = 0, sum_sq_err = 0;
    float min_err = 1e9, max_err = -1e9;
    for (const auto& r : results) {
        sum_err += r.error;
        sum_abs_err += std::abs(r.error);
        sum_sq_err += r.error * r.error;
        min_err = std::min(min_err, r.error);
        max_err = std::max(max_err, r.error);
    }
    float n = static_cast<float>(results.size());
    float mean_err = sum_err / n;
    float mae = sum_abs_err / n;
    float rmse = std::sqrt(sum_sq_err / n);

    printf("\n=== ITM Validation Results ===\n");
    printf("Observations compared: %zu\n", results.size());
    printf("No ITM signal (skipped): %d\n", no_signal);
    printf("\n");
    printf("Mean Error (bias):       %+.1f dB  (positive = ITM predicts stronger)\n", mean_err);
    printf("Mean Absolute Error:     %.1f dB\n", mae);
    printf("RMSE:                    %.1f dB\n", rmse);
    printf("Error range:             %.1f to %+.1f dB\n", min_err, max_err);

    // Breakdown by distance bucket
    printf("\n--- By Distance ---\n");
    printf("%-12s %6s %8s %8s %8s\n", "Distance", "Count", "Bias", "MAE", "RMSE");
    float buckets[] = {0, 1, 2, 5, 10, 20, 50};
    int nbuckets = sizeof(buckets) / sizeof(buckets[0]) - 1;
    for (int b = 0; b < nbuckets; ++b) {
        float lo = buckets[b], hi = buckets[b + 1];
        float s_err = 0, s_abs = 0, s_sq = 0;
        int cnt = 0;
        for (const auto& r : results) {
            if (r.dist_km >= lo && r.dist_km < hi) {
                s_err += r.error;
                s_abs += std::abs(r.error);
                s_sq += r.error * r.error;
                cnt++;
            }
        }
        if (cnt > 0) {
            printf("%5.0f-%2.0fkm  %6d %+7.1f  %7.1f  %7.1f\n",
                   lo, hi, cnt, s_err / cnt, s_abs / cnt,
                   std::sqrt(s_sq / cnt));
        }
    }

    // Per-repeater breakdown
    printf("\n--- By Repeater ---\n");
    printf("%-8s %-30s %6s %8s %8s\n", "ID", "Name", "Count", "Bias", "MAE");
    std::unordered_map<std::string, std::vector<const Result*>> by_rep;
    for (const auto& r : results) by_rep[r.repeater_id].push_back(&r);

    std::vector<std::string> rep_ids;
    for (const auto& [id, _] : by_rep) rep_ids.push_back(id);
    std::sort(rep_ids.begin(), rep_ids.end());

    for (const auto& id : rep_ids) {
        const auto& v = by_rep[id];
        float s_err = 0, s_abs = 0;
        for (const auto* r : v) {
            s_err += r->error;
            s_abs += std::abs(r->error);
        }
        std::string name = rep_map.count(id) ?
            rep_map[id]->name.substr(0, 30) : "?";
        printf("%-8s %-30s %6zu %+7.1f  %7.1f\n",
               id.c_str(), name.c_str(), v.size(),
               s_err / v.size(), s_abs / v.size());
    }

    // Worst predictions (largest absolute error)
    printf("\n--- Worst 10 Predictions ---\n");
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) {
                  return std::abs(a.error) > std::abs(b.error);
              });
    printf("%-8s %-20s %6s %9s %9s %8s\n",
           "Rptr", "Client", "Dist", "Predicted", "Observed", "Error");
    for (int i = 0; i < std::min(10, static_cast<int>(results.size())); ++i) {
        const auto& r = results[i];
        printf("%-8s %-20s %5.1fkm %+8.1fdBm %+8.1fdBm %+7.1fdB\n",
               r.repeater_id.c_str(),
               r.who.substr(0, 20).c_str(),
               r.dist_km, r.predicted, r.observed, r.error);
    }

    return 0;
}
