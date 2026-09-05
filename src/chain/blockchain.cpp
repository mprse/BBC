#include "bbc/chain/blockchain.hpp"

#include "bbc/consensus/proof_of_work.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace bbc::chain {

Blockchain::Blockchain(const std::uint32_t chain_id)
    : stored_blocks_{genesis_block(chain_id)},
      nodes_{BlockNode{}},
      index_by_id_{{stored_blocks_.front().id(), 0}},
      active_indices_{0},
      active_flags_{true},
      blocks_{stored_blocks_.front()} {}

const Block& Blockchain::tip() const noexcept {
    return blocks_.back();
}

std::size_t Blockchain::block_count() const noexcept {
    return blocks_.size();
}

std::size_t Blockchain::stored_block_count() const noexcept {
    return stored_blocks_.size();
}

const std::vector<Block>& Blockchain::blocks() const noexcept {
    return blocks_;
}

const std::vector<Block>& Blockchain::stored_blocks() const noexcept {
    return stored_blocks_;
}

const ChainState& Blockchain::state() const noexcept {
    return state_;
}

bool Blockchain::contains(const crypto::Hash256& id) const {
    return index_by_id_.contains(id);
}

bool Blockchain::is_on_active_chain(const crypto::Hash256& id) const {
    const auto found = index_by_id_.find(id);
    return found != index_by_id_.end() && active_flags_[found->second];
}

std::optional<std::uint64_t> Blockchain::cumulative_work(
    const crypto::Hash256& id
) const {
    const auto found = index_by_id_.find(id);
    if (found == index_by_id_.end()) {
        return std::nullopt;
    }
    return nodes_[found->second].cumulative_work;
}

std::vector<std::pair<std::uint64_t, crypto::Hash256>> Blockchain::block_locator(
    const crypto::Hash256& start,
    const std::size_t maximum_entries
) const {
    std::vector<std::pair<std::uint64_t, crypto::Hash256>> locator;
    const auto found = index_by_id_.find(start);
    if (found == index_by_id_.end() || maximum_entries == 0) {
        return locator;
    }
    locator.reserve(std::min(maximum_entries, stored_blocks_.size()));
    std::size_t index = found->second;
    std::size_t step = 1;
    while (locator.size() < maximum_entries) {
        locator.emplace_back(stored_blocks_[index].height(), stored_blocks_[index].id());
        if (index == 0) {
            break;
        }
        if (locator.size() + 1 == maximum_entries) {
            index = 0;
            continue;
        }
        for (std::size_t skipped = 0; skipped < step && index != 0; ++skipped) {
            index = nodes_[index].parent_index;
        }
        if (locator.size() >= 10 && step <= maximum_entries) {
            step *= 2;
        }
    }
    return locator;
}

AppendResult Blockchain::append(Block block) {
    if (block.is_genesis()) {
        return {ChainError::genesis_cannot_be_appended};
    }
    if (block.chain_id() != tip().chain_id()) {
        return {ChainError::unexpected_chain};
    }
    const crypto::Hash256 block_id = block.id();
    if (index_by_id_.contains(block_id)) {
        return {ChainError::duplicate_block};
    }
    const auto parent = index_by_id_.find(block.previous_block_hash());
    if (parent == index_by_id_.end()) {
        return {ChainError::unexpected_parent};
    }
    const std::size_t parent_index = parent->second;
    const Block& parent_block = stored_blocks_[parent_index];
    if (parent_block.height() == std::numeric_limits<std::uint64_t>::max()) {
        return {ChainError::height_overflow};
    }
    if (block.height() != parent_block.height() + 1) {
        return {ChainError::unexpected_height};
    }
    if (block.timestamp() <= parent_block.timestamp()) {
        return {ChainError::timestamp_not_increasing};
    }
    if (consensus::validate_proof_of_work(block) !=
        consensus::ProofOfWorkError::none) {
        return {ChainError::invalid_proof_of_work};
    }

    ChainState next_state = nodes_[parent_index].state;
    const StateTransitionResult transition = apply_block_state(next_state, block);
    if (!transition.has_value()) {
        return {
            ChainError::invalid_state_transition,
            transition.transaction_index,
            transition.error,
        };
    }

    const std::uint64_t work = nodes_[parent_index].cumulative_work + 1;
    const std::size_t new_index = stored_blocks_.size();
    stored_blocks_.push_back(std::move(block));
    nodes_.push_back({parent_index, work, std::move(next_state)});
    active_flags_.push_back(false);
    index_by_id_.emplace(block_id, new_index);

    AppendResult result;
    if (work <= nodes_[active_indices_.back()].cumulative_work) {
        return result;
    }

    std::vector<std::size_t> next_active_indices;
    for (std::size_t index = new_index;; index = nodes_[index].parent_index) {
        next_active_indices.push_back(index);
        if (index == 0) {
            break;
        }
    }
    std::reverse(next_active_indices.begin(), next_active_indices.end());

    std::size_t common_size = 0;
    while (common_size < active_indices_.size() &&
           common_size < next_active_indices.size() &&
           active_indices_[common_size] == next_active_indices[common_size]) {
        ++common_size;
    }
    for (std::size_t index = common_size; index < active_indices_.size(); ++index) {
        result.detached_blocks.push_back(stored_blocks_[active_indices_[index]]);
    }
    for (std::size_t index = common_size; index < next_active_indices.size(); ++index) {
        result.attached_blocks.push_back(stored_blocks_[next_active_indices[index]]);
    }

    result.active_chain_changed = true;
    result.reorganized = common_size < active_indices_.size();
    active_indices_ = std::move(next_active_indices);
    std::fill(active_flags_.begin(), active_flags_.end(), false);
    for (const std::size_t index : active_indices_) {
        active_flags_[index] = true;
    }
    blocks_.clear();
    blocks_.reserve(active_indices_.size());
    for (const std::size_t index : active_indices_) {
        blocks_.push_back(stored_blocks_[index]);
    }
    state_ = nodes_[active_indices_.back()].state;
    return result;
}

std::string_view chain_error_message(const ChainError error) noexcept {
    switch (error) {
        case ChainError::none:
            return "no error";
        case ChainError::genesis_cannot_be_appended:
            return "Genesis cannot be appended after the canonical chain origin";
        case ChainError::unexpected_chain:
            return "block belongs to a different chain";
        case ChainError::duplicate_block:
            return "block is already stored";
        case ChainError::height_overflow:
            return "chain height is exhausted";
        case ChainError::unexpected_height:
            return "block height does not follow its parent";
        case ChainError::unexpected_parent:
            return "block parent is not stored";
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
