#include "bbc/core/network.hpp"

namespace bbc::core {
namespace {

constexpr crypto::Hash256 target_with_zero_prefix(const std::size_t zero_bytes) {
    crypto::Hash256 target{};
    target.fill(0xFFU);
    for (std::size_t index = 0; index < zero_bytes; ++index) {
        target[index] = 0;
    }
    return target;
}

constexpr crypto::Hash256 regtest_target() {
    crypto::Hash256 target{};
    target.fill(0xFFU);
    target[0] = 0;
    target[1] = 0x0FU;
    return target;
}

constexpr NetworkParameters development{
    NetworkProfile::development,
    "development",
    1,
    {'B', 'B', 'C', 1},
    target_with_zero_prefix(3),
};

constexpr NetworkParameters regtest{
    NetworkProfile::regtest,
    "regtest",
    2,
    {'B', 'B', 'C', 2},
    regtest_target(),
};

}  // namespace

const NetworkParameters& network_parameters(const NetworkProfile profile) noexcept {
    return profile == NetworkProfile::regtest ? regtest : development;
}

const NetworkParameters* network_parameters_for_chain(
    const std::uint32_t chain_id
) noexcept {
    if (chain_id == development.chain_id) {
        return &development;
    }
    if (chain_id == regtest.chain_id) {
        return &regtest;
    }
    return nullptr;
}

std::optional<NetworkProfile> parse_network_profile(
    const std::string_view name
) noexcept {
    if (name == development.name) {
        return NetworkProfile::development;
    }
    if (name == regtest.name) {
        return NetworkProfile::regtest;
    }
    return std::nullopt;
}

}  // namespace bbc::core
