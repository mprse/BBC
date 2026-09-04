#include "bbc/node/node_config.hpp"

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

bool NodeConfig::has_role(const ActorRole role) const noexcept {
    return std::ranges::find(roles, role) != roles.end();
}

NodeConfigResult load_node_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return NodeConfigResult{NodeConfigError::io_error};
    }
    const std::string encoded{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(encoded);
    } catch (const nlohmann::json::exception&) {
        return NodeConfigResult{NodeConfigError::invalid_json};
    }
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_unsigned() ||
        document["schema_version"].get<std::uint64_t>() != 1) {
        return NodeConfigResult{NodeConfigError::unsupported_schema};
    }
    if (!document.contains("name") || !document["name"].is_string()) {
        return NodeConfigResult{NodeConfigError::invalid_name};
    }
    const std::string name = document["name"].get<std::string>();
    if (name.empty() || name.size() > 64 ||
        !std::ranges::all_of(name, [](const unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '-' ||
                character == '_';
        })) {
        return NodeConfigResult{NodeConfigError::invalid_name};
    }
    if (!document.contains("roles") || !document["roles"].is_array() ||
        document["roles"].empty()) {
        return NodeConfigResult{NodeConfigError::invalid_roles};
    }
    std::vector<ActorRole> roles;
    for (const nlohmann::json& encoded_role : document["roles"]) {
        if (!encoded_role.is_string()) {
            return NodeConfigResult{NodeConfigError::invalid_roles};
        }
        const std::optional<ActorRole> role =
            parse_role(encoded_role.get<std::string>());
        if (!role.has_value() || std::ranges::find(roles, *role) != roles.end()) {
            return NodeConfigResult{NodeConfigError::invalid_roles};
        }
        roles.push_back(*role);
    }
    if (!document.contains("data_directory") ||
        !document["data_directory"].is_string() ||
        document["data_directory"].get_ref<const std::string&>().empty()) {
        return NodeConfigResult{NodeConfigError::invalid_data_directory};
    }
    std::uint64_t p2p_port = 0;
    const bool full_node = std::ranges::find(roles, ActorRole::full_node) != roles.end();
    if (full_node) {
        if (!document.contains("p2p") || !document["p2p"].is_object()) {
            return NodeConfigResult{NodeConfigError::invalid_p2p_endpoint};
        }
        const nlohmann::json& p2p = document["p2p"];
        if (!p2p.contains("host") || !p2p["host"].is_string() ||
            p2p["host"].get<std::string>() != "127.0.0.1" ||
            !p2p.contains("port") || !p2p["port"].is_number_unsigned()) {
            return NodeConfigResult{NodeConfigError::invalid_p2p_endpoint};
        }
        p2p_port = p2p["port"].get<std::uint64_t>();
        if (p2p_port > 65'535) {
            return NodeConfigResult{NodeConfigError::invalid_p2p_endpoint};
        }
    } else if (document.contains("p2p")) {
        return NodeConfigResult{NodeConfigError::invalid_p2p_endpoint};
    }
    if (!document.contains("control") || !document["control"].is_object()) {
        return NodeConfigResult{NodeConfigError::invalid_control_endpoint};
    }
    const nlohmann::json& control = document["control"];
    if (!control.contains("host") || !control["host"].is_string() ||
        control["host"].get<std::string>() != "127.0.0.1" ||
        !control.contains("port") || !control["port"].is_number_unsigned()) {
        return NodeConfigResult{NodeConfigError::invalid_control_endpoint};
    }
    const std::uint64_t port = control["port"].get<std::uint64_t>();
    if (port > 65'535) {
        return NodeConfigResult{NodeConfigError::invalid_control_endpoint};
    }
    if (!control.contains("token") || !control["token"].is_string()) {
        return NodeConfigResult{NodeConfigError::invalid_control_token};
    }
    const std::string token = control["token"].get<std::string>();
    if (token.size() < 32 || token.size() > 256) {
        return NodeConfigResult{NodeConfigError::invalid_control_token};
    }

    return NodeConfigResult{NodeConfig{
        name,
        std::move(roles),
        std::filesystem::path{document["data_directory"].get<std::string>()},
        static_cast<std::uint16_t>(p2p_port),
        static_cast<std::uint16_t>(port),
        token,
    }};
}

NodeConfigResult::NodeConfigResult(NodeConfig config)
    : value_(std::move(config)) {}

NodeConfigResult::NodeConfigResult(const NodeConfigError error)
    : value_(error) {}

bool NodeConfigResult::has_value() const noexcept {
    return std::holds_alternative<NodeConfig>(value_);
}

NodeConfig& NodeConfigResult::value() & {
    return std::get<NodeConfig>(value_);
}

const NodeConfig& NodeConfigResult::value() const& {
    return std::get<NodeConfig>(value_);
}

NodeConfig&& NodeConfigResult::value() && {
    return std::get<NodeConfig>(std::move(value_));
}

NodeConfigError NodeConfigResult::error() const noexcept {
    const auto* error = std::get_if<NodeConfigError>(&value_);
    return error == nullptr ? NodeConfigError::none : *error;
}

std::string_view node_config_error_message(const NodeConfigError error) noexcept {
    switch (error) {
        case NodeConfigError::none:
            return "no error";
        case NodeConfigError::io_error:
            return "node configuration file I/O failed";
        case NodeConfigError::invalid_json:
            return "node configuration is not valid JSON";
        case NodeConfigError::unsupported_schema:
            return "node configuration schema is unsupported";
        case NodeConfigError::invalid_name:
            return "node configuration has an invalid actor name";
        case NodeConfigError::invalid_roles:
            return "node configuration has invalid actor roles";
        case NodeConfigError::invalid_data_directory:
            return "node configuration has an invalid data directory";
        case NodeConfigError::invalid_p2p_endpoint:
            return "node configuration has an invalid P2P endpoint";
        case NodeConfigError::invalid_control_endpoint:
            return "node configuration has an invalid control endpoint";
        case NodeConfigError::invalid_control_token:
            return "node configuration has an invalid control token";
    }
    return "unknown node configuration error";
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
