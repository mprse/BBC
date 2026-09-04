#include "app/node_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/node/control_server.hpp"
#include "bbc/node/node_config.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

int run_node(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--config"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> config_path =
        required_option(options, "--config", error_output);
    if (!config_path.has_value()) {
        return usage_error;
    }
    node::NodeConfigResult loaded = node::load_node_config(
        std::filesystem::path{std::string{*config_path}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not load node configuration: "
                     << node::node_config_error_message(loaded.error()) << '\n';
        return runtime_error;
    }
    return node::run_controlled_node(
        std::move(loaded).value(),
        output,
        error_output
    );
}

}  // namespace

void print_node_help(std::ostream& output) {
    output << "Node commands:\n"
           << "  bbc node run --config <path>\n";
}

int run_node_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_node_help(output);
        return success;
    }
    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }
    if (command == "run") {
        return run_node(*options, output, error_output);
    }
    error_output << "Unknown node command: " << command << "\n\n";
    print_node_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
