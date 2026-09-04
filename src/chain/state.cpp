#include "bbc/chain/state.hpp"

#include <limits>
#include <utility>

namespace bbc::chain {

AccountState ChainState::account(const wallet::Address& address) const noexcept {
    const auto found = accounts_.find(address.hash());
    return found == accounts_.end() ? AccountState{} : found->second;
}

std::size_t ChainState::account_count() const noexcept {
    return accounts_.size();
}

const ChainState::Accounts& ChainState::accounts() const noexcept {
    return accounts_;
}

StateTransitionResult apply_block_state(ChainState& state, const Block& block) {
    if (block.is_genesis()) {
        return {StateTransitionError::genesis_has_no_transition, std::nullopt};
    }
    if (!block.reward_recipient().has_value()) {
        return {StateTransitionError::missing_reward_recipient, std::nullopt};
    }

    ChainState next_state = state;
    std::uint64_t total_fees = 0;
    const auto& transactions = block.transactions();
    for (std::size_t index = 0; index < transactions.size(); ++index) {
        const transaction::SignedTransaction& value = transactions[index];
        const wallet::Address sender_address = value.sender();
        AccountState& sender = next_state.accounts_[sender_address.hash()];

        if (value.account_nonce() != sender.next_nonce) {
            return {StateTransitionError::invalid_account_nonce, index};
        }
        if (sender.next_nonce == std::numeric_limits<std::uint64_t>::max()) {
            return {StateTransitionError::account_nonce_exhausted, index};
        }

        const std::uint64_t debit = value.amount() + value.fee();
        if (sender.balance < debit) {
            return {StateTransitionError::insufficient_balance, index};
        }

        sender.balance -= debit;
        ++sender.next_nonce;

        AccountState& recipient = next_state.accounts_[value.recipient().hash()];
        if (recipient.balance >
            std::numeric_limits<std::uint64_t>::max() - value.amount()) {
            return {StateTransitionError::recipient_balance_overflow, index};
        }
        recipient.balance += value.amount();

        if (total_fees > std::numeric_limits<std::uint64_t>::max() - value.fee()) {
            return {StateTransitionError::fee_total_overflow, index};
        }
        total_fees += value.fee();
    }

    if (total_fees >
        std::numeric_limits<std::uint64_t>::max() - block_subsidy) {
        return {StateTransitionError::reward_overflow, std::nullopt};
    }
    const std::uint64_t miner_credit = block_subsidy + total_fees;
    AccountState& miner =
        next_state.accounts_[block.reward_recipient()->hash()];
    if (miner.balance >
        std::numeric_limits<std::uint64_t>::max() - miner_credit) {
        return {StateTransitionError::reward_overflow, std::nullopt};
    }
    miner.balance += miner_credit;

    state = std::move(next_state);
    return {};
}

std::string_view state_transition_error_message(
    const StateTransitionError error
) noexcept {
    switch (error) {
        case StateTransitionError::none:
            return "no error";
        case StateTransitionError::genesis_has_no_transition:
            return "Genesis has no account-state transition";
        case StateTransitionError::missing_reward_recipient:
            return "block has no reward recipient";
        case StateTransitionError::invalid_account_nonce:
            return "transaction account nonce is not the next expected value";
        case StateTransitionError::account_nonce_exhausted:
            return "sender account nonce is exhausted";
        case StateTransitionError::insufficient_balance:
            return "sender balance is insufficient for amount plus fee";
        case StateTransitionError::recipient_balance_overflow:
            return "recipient balance would overflow";
        case StateTransitionError::fee_total_overflow:
            return "block transaction fee total would overflow";
        case StateTransitionError::reward_overflow:
            return "miner reward balance would overflow";
    }
    return "unknown state transition error";
}

}  // namespace bbc::chain
