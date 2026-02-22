#pragma once
#include "types.h"
#include <vector>
#include <cstdint>

namespace meshtile {

class NodeRenderer {
public:
    explicit NodeRenderer(const std::vector<Node>& nodes);

    std::vector<uint8_t> render_tile(int z, int x, int y) const;

    // Draw nodes onto an existing 256x256 RGBA buffer. Returns true if any nodes drawn.
    bool composite_onto(int z, int x, int y, std::vector<uint8_t>& rgba) const;

private:
    const std::vector<Node>& m_nodes;
};

} // namespace meshtile
