#pragma once

#include "bbc/chain/block.hpp"
#include "bbc/crypto/types.hpp"
#include "bbc/wallet/address.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace bbc::chain {

inline constexpr std::uint64_t base_units_per_bbc = 100'000'000;
inline constexpr std::uint64_t block_subsidy = 50 * base_units_per_bbc;

struct AccountState {
    std::uint64_t balance = 0;
    std::uint64_t next_nonce = 0;

    bool operator==(const AccountState&) const = default;
};

struct StateTransitionResult;

class ChainState final {
public:
    [[nodiscard]] AccountState account(const wallet::Address& address) const noexcept;
    [[nodiscard]] std::size_t account_count() const noexcept;

private:
    friend struct StateTransitionResult;
    friend StateTransitionResult apply_block_state(
        ChainState& state,
        const Block& block
    );

    std::map<crypto::Hash256, AccountState> accounts_;
};

enum class StateTransitionError {
    none,
    genesis_has_no_transition,
    missing_reward_recipient,
    invalid_account_nonce,
    account_nonce_exhausted,
    insufficient_balance,
    recipient_balance_overflow,
    fee_total_overflow,
    reward_overflow,
};

struct StateTransitionResult {
    StateTransitionError error = StateTransitionError::none;
    std::optional<std::size_t> transaction_index;

    [[nodiscard]] bool has_value() const noexcept {
        return error == StateTransitionError::none;
    }
};

[[nodiscard]] StateTransitionResult apply_block_state(
    ChainState& state,
    const Block& block
);

[[nodiscard]] std::string_view state_transition_error_message(
    StateTransitionError error
) noexcept;

}  // namespace bbc::chain
