#include "bbc/config/application_config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace bbc::config {
namespace {

using Json = nlohmann::json;

bool contains_only(
    const Json& object,
    const std::initializer_list<std::string_view> allowed
) {
    if (!object.is_object()) {
        return false;
    }
    return std::ranges::all_of(object.items(), [&allowed](const auto& item) {
        return std::ranges::find(allowed, item.key()) != allowed.end();
    });
}

bool valid_name(const std::string_view value) {
    return !value.empty() && value.size() <= 64 &&
        std::ranges::all_of(value, [](const unsigned char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == '-' ||
                character == '_';
        });
}

bool ascii_alphanumeric(const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9');
}

bool valid_ipv4_literal(const std::string_view value) {
    std::size_t part_count = 0;
    std::size_t part_length = 0;
    unsigned int part_value = 0;
    for (const unsigned char character : value) {
        if (character == '.') {
            if (part_length == 0 || part_length > 3 || part_value > 255) {
                return false;
            }
            ++part_count;
            part_length = 0;
            part_value = 0;
            continue;
        }
        if (character < '0' || character > '9') {
            return false;
        }
        ++part_length;
        part_value = part_value * 10 + static_cast<unsigned int>(character - '0');
    }
    return part_count == 3 && part_length > 0 && part_length <= 3 &&
        part_value <= 255;
}

bool valid_host(const std::string_view value) {
    if (value.empty() || value.size() > 253 || !ascii_alphanumeric(value.front()) ||
        !ascii_alphanumeric(value.back())) {
        return false;
    }

    const bool numeric_literal = std::ranges::all_of(
        value,
        [](const unsigned char character) {
            return (character >= '0' && character <= '9') || character == '.';
        }
    );
    if (numeric_literal) {
        return valid_ipv4_literal(value);
    }

    std::size_t label_size = 0;
    bool previous_hyphen = false;
    for (const unsigned char character : value) {
        if (character == '.') {
            if (label_size == 0 || label_size > 63 || previous_hyphen) {
                return false;
            }
            label_size = 0;
            previous_hyphen = false;
            continue;
        }
        if (!ascii_alphanumeric(character) && character != '-') {
            return false;
        }
        if (label_size == 0 && character == '-') {
            return false;
        }
        ++label_size;
        previous_hyphen = character == '-';
    }
    return label_size > 0 && label_size <= 63 && !previous_hyphen;
}

std::optional<NetworkEndpoint> parse_endpoint(const Json& encoded) {
    if (!contains_only(encoded, {"host", "port"}) ||
        !encoded.contains("host") || !encoded["host"].is_string() ||
        !encoded.contains("port") || !encoded["port"].is_number_unsigned()) {
        return std::nullopt;
    }
    const std::string host = encoded["host"].get<std::string>();
    const std::uint64_t port = encoded["port"].get<std::uint64_t>();
    if (!valid_host(host) || port == 0 || port > 65'535) {
        return std::nullopt;
    }
    return NetworkEndpoint{host, static_cast<std::uint16_t>(port)};
}

std::optional<std::filesystem::path> normalized_path(
    const Json& encoded,
    const std::filesystem::path& base_directory
) {
    if (!encoded.is_string()) {
        return std::nullopt;
    }
    const std::string value = encoded.get<std::string>();
    if (value.empty() || value.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    try {
        const std::filesystem::path path{value};
        return (path.is_absolute() ? path : base_directory / path).lexically_normal();
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

std::optional<ApplicationRole> parse_role(const std::string_view value) {
    if (value == "wallet") {
        return ApplicationRole::wallet;
    }
    if (value == "full_node") {
        return ApplicationRole::full_node;
    }
    if (value == "miner") {
        return ApplicationRole::miner;
    }
    return std::nullopt;
}

bool has_role(
    const std::vector<ApplicationRole>& roles,
    const ApplicationRole role
) {
    return std::ranges::find(roles, role) != roles.end();
}

}  // namespace

bool ApplicationConfig::has_role(const ApplicationRole role) const noexcept {
    return config::has_role(roles, role);
}

ApplicationConfigResult load_application_config(
    const std::filesystem::path& path
) {
    std::error_code filesystem_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        return ApplicationConfigResult{ApplicationConfigError::io_error};
    }
    if (file_size > maximum_application_config_size) {
        return ApplicationConfigResult{ApplicationConfigError::file_too_large};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return ApplicationConfigResult{ApplicationConfigError::io_error};
    }
    const std::string encoded{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (input.bad()) {
        return ApplicationConfigResult{ApplicationConfigError::io_error};
    }
    if (encoded.size() > maximum_application_config_size) {
        return ApplicationConfigResult{ApplicationConfigError::file_too_large};
    }

    Json document;
    try {
        document = Json::parse(encoded);
    } catch (const Json::exception&) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_json};
    }
    if (!document.is_object() || !document.contains("schema_version") ||
        !document["schema_version"].is_number_unsigned() ||
        document["schema_version"].get<std::uint64_t>() != 1) {
        return ApplicationConfigResult{ApplicationConfigError::unsupported_schema};
    }
    if (!contains_only(
            document,
            {
                "schema_version",
                "name",
                "network",
                "roles",
                "wallet",
                "data_directory",
                "full_node",
                "miner",
                "rpc",
            }
        )) {
        return ApplicationConfigResult{ApplicationConfigError::unknown_field};
    }

    if (!document.contains("name") || !document["name"].is_string()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_name};
    }
    const std::string name = document["name"].get<std::string>();
    if (!valid_name(name)) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_name};
    }

    if (!document.contains("network") || !document["network"].is_string()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_network_profile};
    }
    const std::optional<core::NetworkProfile> network_profile =
        core::parse_network_profile(document["network"].get<std::string>());
    if (!network_profile.has_value()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_network_profile};
    }

    if (!document.contains("roles") || !document["roles"].is_array() ||
        document["roles"].empty()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_roles};
    }
    std::vector<ApplicationRole> roles;
    for (const Json& encoded_role : document["roles"]) {
        if (!encoded_role.is_string()) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_roles};
        }
        const std::optional<ApplicationRole> role =
            parse_role(encoded_role.get<std::string>());
        if (!role.has_value() || has_role(roles, *role)) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_roles};
        }
        roles.push_back(*role);
    }
    if (!has_role(roles, ApplicationRole::wallet)) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_roles};
    }

    std::error_code absolute_error;
    const std::filesystem::path source_file =
        std::filesystem::absolute(path, absolute_error).lexically_normal();
    if (absolute_error) {
        return ApplicationConfigResult{ApplicationConfigError::io_error};
    }
    const std::filesystem::path base_directory = source_file.parent_path();

    if (!document.contains("wallet") ||
        !contains_only(document["wallet"], {"file"}) ||
        !document["wallet"].contains("file")) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_wallet};
    }
    const std::optional<std::filesystem::path> wallet_file = normalized_path(
        document["wallet"]["file"], base_directory
    );
    if (!wallet_file.has_value()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_wallet};
    }
    if (*wallet_file == source_file) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_wallet};
    }

    if (!document.contains("data_directory")) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_data_directory};
    }
    const std::optional<std::filesystem::path> data_directory = normalized_path(
        document["data_directory"], base_directory
    );
    if (!data_directory.has_value()) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_data_directory};
    }
    if (*data_directory == source_file || *data_directory == *wallet_file) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_data_directory};
    }

    const bool full_node_role = has_role(roles, ApplicationRole::full_node);
    std::optional<FullNodeSettings> full_node;
    if (full_node_role) {
        if (!document.contains("full_node") ||
            !contains_only(document["full_node"], {"listen", "peers"}) ||
            !document["full_node"].contains("listen")) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_full_node};
        }
        const std::optional<NetworkEndpoint> listen =
            parse_endpoint(document["full_node"]["listen"]);
        if (!listen.has_value()) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_full_node};
        }
        std::vector<NetworkEndpoint> peers;
        if (document["full_node"].contains("peers")) {
            const Json& encoded_peers = document["full_node"]["peers"];
            if (!encoded_peers.is_array() ||
                encoded_peers.size() > maximum_initial_peers) {
                return ApplicationConfigResult{ApplicationConfigError::invalid_full_node};
            }
            for (const Json& encoded_peer : encoded_peers) {
                const std::optional<NetworkEndpoint> peer = parse_endpoint(encoded_peer);
                if (!peer.has_value() ||
                    *peer == *listen ||
                    std::ranges::find(peers, *peer) != peers.end()) {
                    return ApplicationConfigResult{ApplicationConfigError::invalid_full_node};
                }
                peers.push_back(*peer);
            }
        }
        full_node = FullNodeSettings{*listen, std::move(peers)};
    } else if (document.contains("full_node")) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_full_node};
    }

    const bool miner_role = has_role(roles, ApplicationRole::miner);
    std::optional<MinerSettings> miner;
    if (miner_role) {
        if (!document.contains("miner") ||
            !contains_only(document["miner"], {"reward_address", "source"}) ||
            !document["miner"].contains("reward_address") ||
            !document["miner"]["reward_address"].is_string()) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_miner};
        }
        const std::optional<wallet::Address> reward_address = wallet::Address::parse(
            document["miner"]["reward_address"].get<std::string>()
        );
        if (!reward_address.has_value()) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_miner};
        }
        std::optional<NetworkEndpoint> source;
        if (document["miner"].contains("source")) {
            source = parse_endpoint(document["miner"]["source"]);
            if (!source.has_value()) {
                return ApplicationConfigResult{ApplicationConfigError::invalid_miner};
            }
        }
        if ((full_node_role && source.has_value()) ||
            (!full_node_role && !source.has_value())) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_miner};
        }
        miner = MinerSettings{*reward_address, source};
    } else if (document.contains("miner")) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_miner};
    }

    const bool long_running = full_node_role || miner_role;
    std::optional<RpcSettings> rpc;
    if (long_running) {
        if (!document.contains("rpc") ||
            !contains_only(document["rpc"], {"listen", "token_file"}) ||
            !document["rpc"].contains("listen") ||
            !document["rpc"].contains("token_file")) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_rpc};
        }
        const std::optional<NetworkEndpoint> listen =
            parse_endpoint(document["rpc"]["listen"]);
        const std::optional<std::filesystem::path> token_file = normalized_path(
            document["rpc"]["token_file"], base_directory
        );
        if (!listen.has_value() || listen->host != "127.0.0.1" ||
            !token_file.has_value()) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_rpc};
        }
        if (full_node.has_value() && full_node->listen == *listen) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_rpc};
        }
        if (*token_file == source_file || *token_file == *wallet_file ||
            *token_file == *data_directory ||
            (miner.has_value() && miner->source == listen)) {
            return ApplicationConfigResult{ApplicationConfigError::invalid_rpc};
        }
        rpc = RpcSettings{*listen, *token_file};
    } else if (document.contains("rpc")) {
        return ApplicationConfigResult{ApplicationConfigError::invalid_rpc};
    }

    return ApplicationConfigResult{ApplicationConfig{
        source_file,
        name,
        *network_profile,
        std::move(roles),
        *wallet_file,
        *data_directory,
        std::move(full_node),
        std::move(miner),
        std::move(rpc),
    }};
}

ApplicationConfigResult::ApplicationConfigResult(ApplicationConfig config)
    : value_(std::move(config)) {}

ApplicationConfigResult::ApplicationConfigResult(
    const ApplicationConfigError error
) : value_(error) {}

bool ApplicationConfigResult::has_value() const noexcept {
    return std::holds_alternative<ApplicationConfig>(value_);
}

ApplicationConfig& ApplicationConfigResult::value() & {
    return std::get<ApplicationConfig>(value_);
}

const ApplicationConfig& ApplicationConfigResult::value() const& {
    return std::get<ApplicationConfig>(value_);
}

ApplicationConfig&& ApplicationConfigResult::value() && {
    return std::get<ApplicationConfig>(std::move(value_));
}

ApplicationConfigError ApplicationConfigResult::error() const noexcept {
    const auto* error = std::get_if<ApplicationConfigError>(&value_);
    return error == nullptr ? ApplicationConfigError::none : *error;
}

std::string_view application_role_name(const ApplicationRole role) noexcept {
    switch (role) {
        case ApplicationRole::wallet:
            return "wallet";
        case ApplicationRole::full_node:
            return "full_node";
        case ApplicationRole::miner:
            return "miner";
    }
    return "unknown";
}

std::string_view application_config_error_message(
    const ApplicationConfigError error
) noexcept {
    switch (error) {
        case ApplicationConfigError::none:
            return "no error";
        case ApplicationConfigError::io_error:
            return "application configuration file I/O failed";
        case ApplicationConfigError::file_too_large:
            return "application configuration exceeds 1 MiB";
        case ApplicationConfigError::invalid_json:
            return "application configuration is not valid JSON";
        case ApplicationConfigError::unsupported_schema:
            return "application configuration schema is unsupported";
        case ApplicationConfigError::unknown_field:
            return "application configuration contains an unknown field";
        case ApplicationConfigError::invalid_name:
            return "application configuration has an invalid name";
        case ApplicationConfigError::invalid_network_profile:
            return "application configuration has an invalid network profile";
        case ApplicationConfigError::invalid_roles:
            return "application configuration has invalid roles";
        case ApplicationConfigError::invalid_wallet:
            return "application configuration has invalid wallet settings";
        case ApplicationConfigError::invalid_data_directory:
            return "application configuration has an invalid data directory";
        case ApplicationConfigError::invalid_full_node:
            return "application configuration has invalid full-node settings";
        case ApplicationConfigError::invalid_miner:
            return "application configuration has invalid miner settings";
        case ApplicationConfigError::invalid_rpc:
            return "application configuration has invalid RPC settings";
    }
    return "unknown application configuration error";
}

}  // namespace bbc::config
