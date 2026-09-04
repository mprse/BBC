#pragma once

#include "bbc/chain/block.hpp"
#include "bbc/chain/state.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace bbc::chain {

enum class ChainError {
    none,
    genesis_cannot_be_appended,
    unexpected_chain,
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

    [[nodiscard]] bool has_value() const noexcept {
        return error == ChainError::none;
    }
};

class Blockchain final {
public:
    explicit Blockchain(std::uint32_t chain_id = 1);

    [[nodiscard]] const Block& tip() const noexcept;
    [[nodiscard]] std::size_t block_count() const noexcept;
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept;
    [[nodiscard]] const ChainState& state() const noexcept;

    [[nodiscard]] AppendResult append(Block block);

private:
    std::vector<Block> blocks_;
    ChainState state_;
};

[[nodiscard]] std::string_view chain_error_message(ChainError error) noexcept;

}  // namespace bbc::chain
