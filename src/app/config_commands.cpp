#include "app/config_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/config/application_config.hpp"
#include "bbc/core/network.hpp"

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

std::string endpoint_text(const config::NetworkEndpoint& endpoint) {
    return endpoint.host + ':' + std::to_string(endpoint.port);
}

std::optional<config::ApplicationConfig> load_config(
    const std::string_view file,
    std::ostream& error_output
) {
    config::ApplicationConfigResult loaded = config::load_application_config(
        std::filesystem::path{std::string{file}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not load application configuration: "
                     << config::application_config_error_message(loaded.error())
                     << '\n';
        return std::nullopt;
    }
    return std::move(loaded).value();
}

int validate_config(
    const std::string_view file,
    std::ostream& output,
    std::ostream& error_output
) {
    const std::optional<config::ApplicationConfig> loaded =
        load_config(file, error_output);
    if (!loaded.has_value()) {
        return runtime_error;
    }
    output << "Configuration validation: success\n"
           << "Configuration: " << loaded->source_file.string() << '\n';
    return success;
}

int show_config(
    const std::string_view file,
    std::ostream& output,
    std::ostream& error_output
) {
    const std::optional<config::ApplicationConfig> loaded =
        load_config(file, error_output);
    if (!loaded.has_value()) {
        return runtime_error;
    }

    output << "Configuration: " << loaded->source_file.string() << '\n'
           << "Name: " << loaded->name << '\n'
           << "Network: "
           << core::network_parameters(loaded->network_profile).name << '\n'
           << "Roles: ";
    for (std::size_t index = 0; index < loaded->roles.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << config::application_role_name(loaded->roles[index]);
    }
    output << '\n'
           << "Wallet file: " << loaded->wallet_file.string() << '\n'
           << "Data directory: " << loaded->data_directory.string() << '\n';

    if (loaded->full_node.has_value()) {
        output << "P2P scope: "
               << config::p2p_scope_name(loaded->full_node->scope) << '\n'
               << "P2P listen: " << endpoint_text(loaded->full_node->listen) << '\n'
               << "Initial peers: " << loaded->full_node->peers.size() << '\n';
        for (const config::NetworkEndpoint& peer : loaded->full_node->peers) {
            output << "Peer: " << endpoint_text(peer) << '\n';
        }
    }
    if (loaded->miner.has_value()) {
        output << "Mining reward address: "
               << loaded->miner->reward_address.value() << '\n'
               << "Mining source: ";
        if (loaded->miner->source.has_value()) {
            output << endpoint_text(*loaded->miner->source);
        } else {
            output << "local full node";
        }
        output << '\n';
    }
    if (loaded->rpc.has_value()) {
        output << "RPC listen: " << endpoint_text(loaded->rpc->listen) << '\n'
               << "RPC token file: " << loaded->rpc->token_file.string() << '\n';
    }
    return success;
}

}  // namespace

void print_config_help(std::ostream& output) {
    output << "Configuration commands:\n"
           << "  bbc config validate --file <path>\n"
           << "  bbc config show --file <path>\n";
}

int run_config_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_config_help(output);
        return success;
    }

    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }
    if (!validate_options(*options, {"--file"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> file =
        required_option(*options, "--file", error_output);
    if (!file.has_value()) {
        return usage_error;
    }
    if (command == "validate") {
        return validate_config(*file, output, error_output);
    }
    if (command == "show") {
        return show_config(*file, output, error_output);
    }

    error_output << "Unknown configuration command: " << command << "\n\n";
    print_config_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
