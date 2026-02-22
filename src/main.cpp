#include "api_client.h"
#include "signal_cache.h"
#include "tile_renderer.h"
#include "node_renderer.h"
#include "noise_map.h"
#include "log.h"
#include "types.h"
#include <httplib.h>
#include <string>
#include <memory>
#include <cstdlib>

int main(int argc, char* argv[]) {
    meshtile::log_set_level(meshtile::LogLevel::Info);

    int port = 8080;
    std::string host = "0.0.0.0";
    std::string nodes_source;
    std::string noise_source;
    float max_range = 30.0f;

    meshtile::RfConfig rf_config;
    std::string colormap_name;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)     port = std::atoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--nodes" && i + 1 < argc) nodes_source = argv[++i];
        else if (arg == "--noise-data" && i + 1 < argc) noise_source = argv[++i];
        else if (arg == "--max-range" && i + 1 < argc) max_range = std::atof(argv[++i]);
        else if (arg == "--climate" && i + 1 < argc) rf_config.climate = std::atoi(argv[++i]);
        else if (arg == "--clutter-height" && i + 1 < argc) rf_config.clutter_height_m = std::atof(argv[++i]);
        else if (arg == "--time-pct" && i + 1 < argc) rf_config.time_pct = std::atof(argv[++i]);
        else if (arg == "--location-pct" && i + 1 < argc) rf_config.location_pct = std::atof(argv[++i]);
        else if (arg == "--situation-pct" && i + 1 < argc) rf_config.situation_pct = std::atof(argv[++i]);
        else if (arg == "--ground-dielectric" && i + 1 < argc) rf_config.ground_dielectric = std::atof(argv[++i]);
        else if (arg == "--ground-conductivity" && i + 1 < argc) rf_config.ground_conductivity = std::atof(argv[++i]);
        else if (arg == "--refractivity" && i + 1 < argc) rf_config.refractivity = std::atof(argv[++i]);
        else if (arg == "--colormap" && i + 1 < argc) colormap_name = argv[++i];
    }

    if (!colormap_name.empty()) {
        if (colormap_name == "plasma") rf_config.colormap = meshtile::Colormap::plasma;
        else if (colormap_name == "viridis") rf_config.colormap = meshtile::Colormap::viridis;
        else if (colormap_name == "turbo") rf_config.colormap = meshtile::Colormap::turbo;
        else if (colormap_name == "inferno") rf_config.colormap = meshtile::Colormap::inferno;
        else if (colormap_name == "red_yellow_green") rf_config.colormap = meshtile::Colormap::red_yellow_green;
        else LOG_WARN("Unknown colormap '%s', using default", colormap_name.c_str());
    }

    // Log effective configuration
    auto colormap_str = [](meshtile::Colormap c) -> const char* {
        switch (c) {
            case meshtile::Colormap::plasma:           return "plasma";
            case meshtile::Colormap::viridis:          return "viridis";
            case meshtile::Colormap::turbo:            return "turbo";
            case meshtile::Colormap::inferno:          return "inferno";
            case meshtile::Colormap::red_yellow_green: return "red_yellow_green";
            default:                                   return "red_yellow_green";
        }
    };
    LOG_INFO("Configuration:");
    LOG_INFO("  host=%s port=%d max-range=%.1f km", host.c_str(), port, max_range);
    LOG_INFO("  climate=%d refractivity=%.1f polarization=%d",
             rf_config.climate, rf_config.refractivity, rf_config.polarization);
    LOG_INFO("  ground-dielectric=%.1f ground-conductivity=%.4f",
             rf_config.ground_dielectric, rf_config.ground_conductivity);
    LOG_INFO("  clutter-height=%.1f m", rf_config.clutter_height_m);
    LOG_INFO("  time-pct=%.1f location-pct=%.1f situation-pct=%.1f",
             rf_config.time_pct, rf_config.location_pct, rf_config.situation_pct);
    LOG_INFO("  colormap=%s", colormap_str(rf_config.colormap));

    // Fetch nodes
    meshtile::ApiClientConfig api_config;
    if (!nodes_source.empty()) api_config.source = nodes_source;
    api_config.default_max_range_km = max_range;

    auto nodes = meshtile::fetch_nodes(api_config);
    LOG_INFO("Loaded %zu nodes", nodes.size());

    if (nodes.empty()) {
        LOG_ERROR("No nodes loaded. Exiting.");
        return 1;
    }

    // Load noise floor map (default: meshmapper API, or local file via --noise-data)
    std::unique_ptr<meshtile::NoiseMap> noise_map;
    {
        std::string src = noise_source.empty()
            ? "https://den.meshmapper.net/api.php?request=map_data"
            : noise_source;
        auto obs = meshtile::load_noise_observations(src);
        if (!obs.empty()) {
            noise_map = std::make_unique<meshtile::NoiseMap>(obs);
        }
    }

    // Pre-compute signal grids
    meshtile::SignalCache cache;
    cache.precompute(nodes, rf_config);

    // Start HTTP server
    meshtile::NodeRenderer node_renderer(nodes);
    meshtile::TileRenderer renderer(cache, rf_config, noise_map.get(), &node_renderer);
    httplib::Server svr;

    svr.Get(R"(/tiles/(\d+)/(\d+)/(\d+)\.png)",
        [&renderer](const httplib::Request& req, httplib::Response& res) {
            int z = std::stoi(req.matches[1]);
            int x = std::stoi(req.matches[2]);
            int y = std::stoi(req.matches[3]);

            auto png = renderer.render_tile(z, x, y);
            res.set_content(
                std::string(reinterpret_cast<const char*>(png.data()), png.size()),
                "image/png");
            res.set_header("Cache-Control", "public, max-age=3600");
            res.set_header("Access-Control-Allow-Origin", "*");
        });

    svr.Get(R"(/signal/(\d+)/(\d+)/(\d+)\.png)",
        [&renderer](const httplib::Request& req, httplib::Response& res) {
            int z = std::stoi(req.matches[1]);
            int x = std::stoi(req.matches[2]);
            int y = std::stoi(req.matches[3]);

            auto png = renderer.render_signal(z, x, y);
            res.set_content(
                std::string(reinterpret_cast<const char*>(png.data()), png.size()),
                "image/png");
            res.set_header("Cache-Control", "public, max-age=3600");
            res.set_header("Access-Control-Allow-Origin", "*");
        });

    svr.Get(R"(/nodes/(\d+)/(\d+)/(\d+)\.png)",
        [&node_renderer](const httplib::Request& req, httplib::Response& res) {
            int z = std::stoi(req.matches[1]);
            int x = std::stoi(req.matches[2]);
            int y = std::stoi(req.matches[3]);

            auto png = node_renderer.render_tile(z, x, y);
            res.set_content(
                std::string(reinterpret_cast<const char*>(png.data()), png.size()),
                "image/png");
            res.set_header("Cache-Control", "public, max-age=3600");
            res.set_header("Access-Control-Allow-Origin", "*");
        });

    svr.Get("/overlay.kml",
        [&host, &port, &cache](const httplib::Request&, httplib::Response& res) {
            auto bounds = cache.coverage_bounds();
            std::string base = "http://" + host + ":" + std::to_string(port);

            std::string kml = R"(<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
<Document>
  <name>Meshtastic Coverage</name>
  <description>RF signal strength overlay</description>
  <GroundOverlay>
    <name>Signal Coverage</name>
    <color>ffffffff</color>
    <Icon>
      <href>)" + base + R"(/tiles/{z}/{x}/{y}.png</href>
    </Icon>
    <LatLonBox>
      <north>)" + std::to_string(bounds.max_lat) + R"(</north>
      <south>)" + std::to_string(bounds.min_lat) + R"(</south>
      <east>)" + std::to_string(bounds.max_lon) + R"(</east>
      <west>)" + std::to_string(bounds.min_lon) + R"(</west>
    </LatLonBox>
  </GroundOverlay>
</Document>
</kml>)";
            res.set_content(kml, "application/vnd.google-earth.kml+xml");
        });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain");
    });

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        LOG_INFO("%s %s %d %zuB",
                 req.method.c_str(), req.path.c_str(),
                 res.status, res.body.size());
    });

    LOG_INFO("Tile server on %s:%d", host.c_str(), port);
    LOG_INFO("  Tiles:  http://%s:%d/tiles/{z}/{x}/{y}.png  (signal + nodes)", host.c_str(), port);
    LOG_INFO("  Signal: http://%s:%d/signal/{z}/{x}/{y}.png (signal only)", host.c_str(), port);
    LOG_INFO("  Nodes:  http://%s:%d/nodes/{z}/{x}/{y}.png  (nodes only)", host.c_str(), port);
    LOG_INFO("  KML:   http://%s:%d/overlay.kml", host.c_str(), port);
    svr.listen(host, port);

    return 0;
}
