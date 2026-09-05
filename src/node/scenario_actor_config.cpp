#include "bbc/node/scenario_actor_config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace bbc::node {
namespace {

std::optional<ActorRole> parse_role(const std::string_view value) {
    if (value == "wallet") {
        return ActorRole::wallet;
    }
    if (value == "full_node") {
        return ActorRole::full_node;
    }
    if (value == "miner") {
        return ActorRole::miner;
    }
    return std::nullopt;
}

}  // namespace

bool ScenarioActorConfig::has_role(const ActorRole role) const noexcept {
    return std::ranges::find(roles, role) != roles.end();
}

ScenarioActorConfigResult load_scenario_actor_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::io_error};
    }
    const std::string encoded{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(encoded);
    } catch (const nlohmann::json::exception&) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_json};
    }
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_unsigned() ||
        document["schema_version"].get<std::uint64_t>() != 1) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::unsupported_schema};
    }
    if (!document.contains("name") || !document["name"].is_string()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_name};
    }
    const std::string name = document["name"].get<std::string>();
    if (name.empty() || name.size() > 64 ||
        !std::ranges::all_of(name, [](const unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '-' ||
                character == '_';
        })) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_name};
    }
    if (!document.contains("roles") || !document["roles"].is_array() ||
        document["roles"].empty()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_roles};
    }
    std::vector<ActorRole> roles;
    for (const nlohmann::json& encoded_role : document["roles"]) {
        if (!encoded_role.is_string()) {
            return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_roles};
        }
        const std::optional<ActorRole> role =
            parse_role(encoded_role.get<std::string>());
        if (!role.has_value() || std::ranges::find(roles, *role) != roles.end()) {
            return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_roles};
        }
        roles.push_back(*role);
    }
    if (!document.contains("data_directory") ||
        !document["data_directory"].is_string() ||
        document["data_directory"].get_ref<const std::string&>().empty()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_data_directory};
    }
    if (!document.contains("network") || !document["network"].is_string()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_network_profile};
    }
    const std::optional<core::NetworkProfile> network_profile =
        core::parse_network_profile(document["network"].get<std::string>());
    if (!network_profile.has_value()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_network_profile};
    }
    std::uint64_t p2p_port = 0;
    const bool full_node = std::ranges::find(roles, ActorRole::full_node) != roles.end();
    if (full_node) {
        if (!document.contains("p2p") || !document["p2p"].is_object()) {
            return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_p2p_endpoint};
        }
        const nlohmann::json& p2p = document["p2p"];
        if (!p2p.contains("host") || !p2p["host"].is_string() ||
            p2p["host"].get<std::string>() != "127.0.0.1" ||
            !p2p.contains("port") || !p2p["port"].is_number_unsigned()) {
            return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_p2p_endpoint};
        }
        p2p_port = p2p["port"].get<std::uint64_t>();
        if (p2p_port > 65'535) {
            return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_p2p_endpoint};
        }
    } else if (document.contains("p2p")) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_p2p_endpoint};
    }
    if (!document.contains("control") || !document["control"].is_object()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_control_endpoint};
    }
    const nlohmann::json& control = document["control"];
    if (!control.contains("host") || !control["host"].is_string() ||
        control["host"].get<std::string>() != "127.0.0.1" ||
        !control.contains("port") || !control["port"].is_number_unsigned()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_control_endpoint};
    }
    const std::uint64_t port = control["port"].get<std::uint64_t>();
    if (port > 65'535) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_control_endpoint};
    }
    if (!control.contains("token") || !control["token"].is_string()) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_control_token};
    }
    const std::string token = control["token"].get<std::string>();
    if (token.size() < 32 || token.size() > 256) {
        return ScenarioActorConfigResult{ScenarioActorConfigError::invalid_control_token};
    }

    return ScenarioActorConfigResult{ScenarioActorConfig{
        name,
        std::move(roles),
        std::filesystem::path{document["data_directory"].get<std::string>()},
        *network_profile,
        static_cast<std::uint16_t>(p2p_port),
        static_cast<std::uint16_t>(port),
        token,
    }};
}

ScenarioActorConfigResult::ScenarioActorConfigResult(ScenarioActorConfig config)
    : value_(std::move(config)) {}

ScenarioActorConfigResult::ScenarioActorConfigResult(const ScenarioActorConfigError error)
    : value_(error) {}

bool ScenarioActorConfigResult::has_value() const noexcept {
    return std::holds_alternative<ScenarioActorConfig>(value_);
}

ScenarioActorConfig& ScenarioActorConfigResult::value() & {
    return std::get<ScenarioActorConfig>(value_);
}

const ScenarioActorConfig& ScenarioActorConfigResult::value() const& {
    return std::get<ScenarioActorConfig>(value_);
}

ScenarioActorConfig&& ScenarioActorConfigResult::value() && {
    return std::get<ScenarioActorConfig>(std::move(value_));
}

ScenarioActorConfigError ScenarioActorConfigResult::error() const noexcept {
    const auto* error = std::get_if<ScenarioActorConfigError>(&value_);
    return error == nullptr ? ScenarioActorConfigError::none : *error;
}

std::string_view scenario_actor_config_error_message(
    const ScenarioActorConfigError error
) noexcept {
    switch (error) {
        case ScenarioActorConfigError::none:
            return "no error";
        case ScenarioActorConfigError::io_error:
            return "scenario actor configuration file I/O failed";
        case ScenarioActorConfigError::invalid_json:
            return "scenario actor configuration is not valid JSON";
        case ScenarioActorConfigError::unsupported_schema:
            return "scenario actor configuration schema is unsupported";
        case ScenarioActorConfigError::invalid_name:
            return "scenario actor configuration has an invalid actor name";
        case ScenarioActorConfigError::invalid_roles:
            return "scenario actor configuration has invalid actor roles";
        case ScenarioActorConfigError::invalid_data_directory:
            return "scenario actor configuration has an invalid data directory";
        case ScenarioActorConfigError::invalid_network_profile:
            return "scenario actor configuration has an invalid network profile";
        case ScenarioActorConfigError::invalid_p2p_endpoint:
            return "scenario actor configuration has an invalid P2P endpoint";
        case ScenarioActorConfigError::invalid_control_endpoint:
            return "scenario actor configuration has an invalid control endpoint";
        case ScenarioActorConfigError::invalid_control_token:
            return "scenario actor configuration has an invalid control token";
    }
    return "unknown scenario actor configuration error";
}

std::string_view actor_role_name(const ActorRole role) noexcept {
    switch (role) {
        case ActorRole::wallet:
            return "wallet";
        case ActorRole::full_node:
            return "full_node";
        case ActorRole::miner:
            return "miner";
    }
    return "unknown";
}

}  // namespace bbc::node
