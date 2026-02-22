#include "api_client.h"
#include "signal_cache.h"
#include "tile_renderer.h"
#include "node_renderer.h"
#include "log.h"
#include "types.h"
#include <httplib.h>
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    meshtile::log_set_level(meshtile::LogLevel::Info);

    int port = 8080;
    std::string host = "0.0.0.0";
    std::string nodes_source;
    float max_range = 30.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)     port = std::atoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--nodes" && i + 1 < argc) nodes_source = argv[++i];
        else if (arg == "--max-range" && i + 1 < argc) max_range = std::atof(argv[++i]);
    }

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

    // Pre-compute signal grids
    meshtile::RfConfig rf_config;
    meshtile::SignalCache cache;
    cache.precompute(nodes, rf_config);

    // Start HTTP server
    meshtile::TileRenderer renderer(cache, rf_config);
    meshtile::NodeRenderer node_renderer(nodes);
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
    LOG_INFO("  Tiles: http://%s:%d/tiles/{z}/{x}/{y}.png", host.c_str(), port);
    LOG_INFO("  Nodes: http://%s:%d/nodes/{z}/{x}/{y}.png", host.c_str(), port);
    LOG_INFO("  KML:   http://%s:%d/overlay.kml", host.c_str(), port);
    svr.listen(host, port);

    return 0;
}
