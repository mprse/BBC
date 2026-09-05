#pragma once

#include "bbc/core/network.hpp"
#include "bbc/wallet/address.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bbc::config {

inline constexpr std::size_t maximum_application_config_size = 1'048'576;
inline constexpr std::size_t maximum_initial_peers = 64;

enum class ApplicationRole {
    wallet,
    full_node,
    miner,
};

struct NetworkEndpoint {
    std::string host;
    std::uint16_t port;

    bool operator==(const NetworkEndpoint&) const = default;
};

struct FullNodeSettings {
    NetworkEndpoint listen;
    std::vector<NetworkEndpoint> peers;
};

struct MinerSettings {
    wallet::Address reward_address;
    std::optional<NetworkEndpoint> source;
};

struct RpcSettings {
    NetworkEndpoint listen;
    std::filesystem::path token_file;
};

struct ApplicationConfig {
    std::filesystem::path source_file;
    std::string name;
    core::NetworkProfile network_profile;
    std::vector<ApplicationRole> roles;
    std::filesystem::path wallet_file;
    std::filesystem::path data_directory;
    std::optional<FullNodeSettings> full_node;
    std::optional<MinerSettings> miner;
    std::optional<RpcSettings> rpc;

    [[nodiscard]] bool has_role(ApplicationRole role) const noexcept;
};

enum class ApplicationConfigError {
    none,
    io_error,
    file_too_large,
    invalid_json,
    unsupported_schema,
    unknown_field,
    invalid_name,
    invalid_network_profile,
    invalid_roles,
    invalid_wallet,
    invalid_data_directory,
    invalid_full_node,
    invalid_miner,
    invalid_rpc,
};

class ApplicationConfigResult final {
public:
    explicit ApplicationConfigResult(ApplicationConfig config);
    explicit ApplicationConfigResult(ApplicationConfigError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] ApplicationConfig& value() &;
    [[nodiscard]] const ApplicationConfig& value() const&;
    [[nodiscard]] ApplicationConfig&& value() &&;
    [[nodiscard]] ApplicationConfigError error() const noexcept;

private:
    std::variant<ApplicationConfig, ApplicationConfigError> value_;
};

[[nodiscard]] ApplicationConfigResult load_application_config(
    const std::filesystem::path& path
);

[[nodiscard]] std::string_view application_role_name(
    ApplicationRole role
) noexcept;

[[nodiscard]] std::string_view application_config_error_message(
    ApplicationConfigError error
) noexcept;

}  // namespace bbc::config
