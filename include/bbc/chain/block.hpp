#pragma once

#include "bbc/core/protocol.hpp"
#include "bbc/crypto/types.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace bbc::chain {

inline constexpr std::size_t block_header_size = 165;
inline constexpr std::size_t block_difficulty_target_offset = 121;
inline constexpr std::size_t block_mining_nonce_offset = 153;
inline constexpr std::size_t maximum_transactions_per_block = 1000;
inline constexpr std::size_t maximum_block_size =
    block_header_size +
    maximum_transactions_per_block * transaction::signed_transaction_size;

enum class BlockError {
    none,
    too_many_transactions,
    duplicate_transaction,
    invalid_size,
    invalid_magic,
    unsupported_version,
    unsupported_chain,
    invalid_height,
    invalid_previous_hash,
    invalid_reward_recipient,
    invalid_transaction,
    invalid_transaction_root,
    invalid_genesis,
    file_already_exists,
    io_error,
};

struct BlockFields {
    std::uint64_t height;
    crypto::Hash256 previous_block_hash;
    wallet::Address reward_recipient;
    std::uint64_t timestamp;
    crypto::Hash256 difficulty_target;
    std::uint64_t mining_nonce;
    std::vector<transaction::SignedTransaction> transactions;
    std::uint32_t chain_id = core::chain_id;
};

class BlockResult;

class Block final {
public:
    [[nodiscard]] std::uint32_t chain_id() const noexcept;
    [[nodiscard]] std::uint64_t height() const noexcept;
    [[nodiscard]] const crypto::Hash256& previous_block_hash() const noexcept;
    [[nodiscard]] const crypto::Hash256& transaction_root() const noexcept;
    [[nodiscard]] const std::optional<wallet::Address>& reward_recipient() const noexcept;
    [[nodiscard]] std::uint64_t timestamp() const noexcept;
    [[nodiscard]] const crypto::Hash256& difficulty_target() const noexcept;
    [[nodiscard]] std::uint64_t mining_nonce() const noexcept;
    [[nodiscard]] const std::vector<transaction::SignedTransaction>& transactions() const noexcept;
    [[nodiscard]] bool is_genesis() const noexcept;

    [[nodiscard]] crypto::Bytes serialize_header() const;
    [[nodiscard]] crypto::Bytes serialize() const;
    [[nodiscard]] crypto::Hash256 id() const;
    [[nodiscard]] Block with_mining_nonce(std::uint64_t mining_nonce) const;

private:
    friend BlockResult create_block(BlockFields fields);
    friend BlockResult deserialize_block(crypto::ByteView encoded);
    friend Block genesis_block();

    Block(
        std::uint32_t chain_id,
        std::uint64_t height,
        crypto::Hash256 previous_block_hash,
        crypto::Hash256 transaction_root,
        std::optional<wallet::Address> reward_recipient,
        std::uint64_t timestamp,
        crypto::Hash256 difficulty_target,
        std::uint64_t mining_nonce,
        std::vector<transaction::SignedTransaction> transactions
    );

    std::uint32_t chain_id_;
    std::uint64_t height_;
    crypto::Hash256 previous_block_hash_{};
    crypto::Hash256 transaction_root_{};
    std::optional<wallet::Address> reward_recipient_;
    std::uint64_t timestamp_;
    crypto::Hash256 difficulty_target_{};
    std::uint64_t mining_nonce_;
    std::vector<transaction::SignedTransaction> transactions_;
};

class BlockResult final {
public:
    explicit BlockResult(Block block);
    explicit BlockResult(BlockError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] Block& value() &;
    [[nodiscard]] const Block& value() const&;
    [[nodiscard]] Block&& value() &&;
    [[nodiscard]] BlockError error() const noexcept;

private:
    std::variant<Block, BlockError> value_;
};

[[nodiscard]] crypto::Hash256 calculate_transaction_root(
    const std::vector<transaction::SignedTransaction>& transactions
);

[[nodiscard]] BlockResult create_block(BlockFields fields);
[[nodiscard]] BlockResult deserialize_block(crypto::ByteView encoded);
[[nodiscard]] Block genesis_block();

[[nodiscard]] BlockError save_block(
    const Block& block,
    const std::filesystem::path& path
);

[[nodiscard]] BlockResult load_block(const std::filesystem::path& path);

[[nodiscard]] std::string_view block_error_message(BlockError error) noexcept;

}  // namespace bbc::chain
