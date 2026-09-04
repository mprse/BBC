#pragma once

#include "bbc/chain/block.hpp"
#include "bbc/crypto/types.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <variant>

namespace bbc::consensus {

enum class ProofOfWorkError {
    none,
    genesis_not_mineable,
    unexpected_target,
    target_not_met,
    attempt_limit_reached,
    nonce_exhausted,
};

class MiningResult final {
public:
    MiningResult(chain::Block block, std::uint64_t attempts);
    MiningResult(
        ProofOfWorkError error,
        std::uint64_t attempts,
        std::optional<std::uint64_t> next_nonce = std::nullopt
    );

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] chain::Block& value() &;
    [[nodiscard]] const chain::Block& value() const&;
    [[nodiscard]] chain::Block&& value() &&;
    [[nodiscard]] ProofOfWorkError error() const noexcept;
    [[nodiscard]] std::uint64_t attempts() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> next_nonce() const noexcept;

private:
    std::variant<chain::Block, ProofOfWorkError> value_;
    std::uint64_t attempts_;
    std::optional<std::uint64_t> next_nonce_;
};

[[nodiscard]] const crypto::Hash256& fixed_difficulty_target() noexcept;

[[nodiscard]] bool hash_meets_target(
    const crypto::Hash256& hash,
    const crypto::Hash256& target
) noexcept;

[[nodiscard]] ProofOfWorkError validate_proof_of_work(
    const chain::Block& block
);

[[nodiscard]] MiningResult mine_block(
    const chain::Block& candidate,
    std::uint64_t maximum_attempts = std::numeric_limits<std::uint64_t>::max()
);

[[nodiscard]] std::string_view proof_of_work_error_message(
    ProofOfWorkError error
) noexcept;

}  // namespace bbc::consensus
