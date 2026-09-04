#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bbc::node {

enum class ActorRole {
    wallet,
    full_node,
    miner,
};

struct NodeConfig {
    std::string name;
    std::vector<ActorRole> roles;
    std::filesystem::path data_directory;
    std::uint16_t p2p_port = 0;
    std::uint16_t control_port = 0;
    std::string control_token;

    [[nodiscard]] bool has_role(ActorRole role) const noexcept;
};

enum class NodeConfigError {
    none,
    io_error,
    invalid_json,
    unsupported_schema,
    invalid_name,
    invalid_roles,
    invalid_data_directory,
    invalid_p2p_endpoint,
    invalid_control_endpoint,
    invalid_control_token,
};

class NodeConfigResult final {
public:
    explicit NodeConfigResult(NodeConfig config);
    explicit NodeConfigResult(NodeConfigError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] NodeConfig& value() &;
    [[nodiscard]] const NodeConfig& value() const&;
    [[nodiscard]] NodeConfig&& value() &&;
    [[nodiscard]] NodeConfigError error() const noexcept;

private:
    std::variant<NodeConfig, NodeConfigError> value_;
};

[[nodiscard]] NodeConfigResult load_node_config(
    const std::filesystem::path& path
);
[[nodiscard]] std::string_view node_config_error_message(
    NodeConfigError error
) noexcept;
[[nodiscard]] std::string_view actor_role_name(ActorRole role) noexcept;

}  // namespace bbc::node
