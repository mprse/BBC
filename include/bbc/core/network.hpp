#pragma once

#include "bbc/crypto/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bbc::core {

enum class NetworkProfile {
    development,
    regtest,
};

struct NetworkParameters {
    NetworkProfile profile;
    std::string_view name;
    std::uint32_t chain_id;
    std::array<crypto::Byte, 4> p2p_magic;
    crypto::Hash256 difficulty_target;
};

[[nodiscard]] const NetworkParameters& network_parameters(
    NetworkProfile profile
) noexcept;
[[nodiscard]] const NetworkParameters* network_parameters_for_chain(
    std::uint32_t chain_id
) noexcept;
[[nodiscard]] std::optional<NetworkProfile> parse_network_profile(
    std::string_view name
) noexcept;

}  // namespace bbc::core
