#include "bbc/chain/blockchain.hpp"

#include "bbc/consensus/proof_of_work.hpp"

#include <limits>
#include <utility>

namespace bbc::chain {

Blockchain::Blockchain() : blocks_{genesis_block()} {}

const Block& Blockchain::tip() const noexcept {
    return blocks_.back();
}

std::size_t Blockchain::block_count() const noexcept {
    return blocks_.size();
}

const ChainState& Blockchain::state() const noexcept {
    return state_;
}

AppendResult Blockchain::append(Block block) {
    if (block.is_genesis()) {
        return {ChainError::genesis_cannot_be_appended};
    }
    if (tip().height() == std::numeric_limits<std::uint64_t>::max()) {
        return {ChainError::height_overflow};
    }
    if (block.height() != tip().height() + 1) {
        return {ChainError::unexpected_height};
    }
    if (block.previous_block_hash() != tip().id()) {
        return {ChainError::unexpected_parent};
    }
    if (block.timestamp() <= tip().timestamp()) {
        return {ChainError::timestamp_not_increasing};
    }
    if (consensus::validate_proof_of_work(block) !=
        consensus::ProofOfWorkError::none) {
        return {ChainError::invalid_proof_of_work};
    }

    ChainState next_state = state_;
    const StateTransitionResult transition = apply_block_state(next_state, block);
    if (!transition.has_value()) {
        return {
            ChainError::invalid_state_transition,
            transition.transaction_index,
            transition.error,
        };
    }

    state_ = std::move(next_state);
    blocks_.push_back(std::move(block));
    return {};
}

std::string_view chain_error_message(const ChainError error) noexcept {
    switch (error) {
        case ChainError::none:
            return "no error";
        case ChainError::genesis_cannot_be_appended:
            return "Genesis cannot be appended after the canonical chain origin";
        case ChainError::height_overflow:
            return "chain height is exhausted";
        case ChainError::unexpected_height:
            return "block height does not extend the current chain tip";
        case ChainError::unexpected_parent:
            return "block previous hash does not match the current chain tip";
        case ChainError::timestamp_not_increasing:
            return "block timestamp must be greater than its parent's timestamp";
        case ChainError::invalid_proof_of_work:
            return "block Proof of Work is invalid";
        case ChainError::invalid_state_transition:
            return "block account-state transition is invalid";
    }
    return "unknown chain error";
}

}  // namespace bbc::chain
