#pragma once

#include "bbc/chain/block.hpp"
#include "bbc/chain/state.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace bbc::chain {

enum class ChainError {
    none,
    genesis_cannot_be_appended,
    unexpected_chain,
    duplicate_block,
    height_overflow,
    unexpected_height,
    unexpected_parent,
    timestamp_not_increasing,
    invalid_proof_of_work,
    invalid_state_transition,
};

struct AppendResult {
    ChainError error = ChainError::none;
    std::optional<std::size_t> transaction_index;
    StateTransitionError state_error = StateTransitionError::none;
    bool active_chain_changed = false;
    bool reorganized = false;
    std::vector<Block> detached_blocks;
    std::vector<Block> attached_blocks;

    [[nodiscard]] bool has_value() const noexcept {
        return error == ChainError::none;
    }
};

class Blockchain final {
public:
    explicit Blockchain(std::uint32_t chain_id = 1);

    [[nodiscard]] const Block& tip() const noexcept;
    [[nodiscard]] std::size_t block_count() const noexcept;
    [[nodiscard]] std::size_t stored_block_count() const noexcept;
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept;
    [[nodiscard]] const std::vector<Block>& stored_blocks() const noexcept;
    [[nodiscard]] const ChainState& state() const noexcept;
    [[nodiscard]] bool contains(const crypto::Hash256& id) const;
    [[nodiscard]] bool is_on_active_chain(const crypto::Hash256& id) const;
    [[nodiscard]] std::optional<std::uint64_t> cumulative_work(
        const crypto::Hash256& id
    ) const;
    [[nodiscard]] std::vector<std::pair<std::uint64_t, crypto::Hash256>>
    block_locator(
        const crypto::Hash256& start,
        std::size_t maximum_entries
    ) const;

    [[nodiscard]] AppendResult append(Block block);

private:
    struct BlockNode {
        std::size_t parent_index = 0;
        std::uint64_t cumulative_work = 0;
        ChainState state;
    };

    std::vector<Block> stored_blocks_;
    std::vector<BlockNode> nodes_;
    std::map<crypto::Hash256, std::size_t> index_by_id_;
    std::vector<std::size_t> active_indices_;
    std::vector<bool> active_flags_;
    std::vector<Block> blocks_;
    ChainState state_;
};

[[nodiscard]] std::string_view chain_error_message(ChainError error) noexcept;

}  // namespace bbc::chain
