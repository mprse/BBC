#pragma once

#include "bbc/node/node_config.hpp"

#include <iosfwd>

namespace bbc::node {

[[nodiscard]] int run_controlled_node(
    NodeConfig config,
    std::ostream& output,
    std::ostream& error_output
);

}  // namespace bbc::node
