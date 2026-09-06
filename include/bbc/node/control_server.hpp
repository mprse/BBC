#pragma once

#include "bbc/config/application_config.hpp"
#include "bbc/node/scenario_actor_config.hpp"

#include <iosfwd>

namespace bbc::node {

[[nodiscard]] int run_controlled_node(
    ScenarioActorConfig config,
    std::ostream& output,
    std::ostream& error_output
);

[[nodiscard]] int run_application_node(
    config::ApplicationConfig config,
    std::ostream& output,
    std::ostream& error_output
);

}  // namespace bbc::node
