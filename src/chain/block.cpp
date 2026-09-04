#include "bbc/chain/block.hpp"

#include "bbc/crypto/hash.hpp"
#include "bbc/core/network.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace bbc::chain {
namespace {

constexpr std::array<crypto::Byte, 4> block_magic{'B', 'B', 'L', 'K'};
constexpr crypto::Byte block_version = 1;
constexpr crypto::Byte merkle_leaf_domain = 0;
constexpr crypto::Byte merkle_node_domain = 1;
constexpr crypto::Byte empty_root_domain = 2;

constexpr std::size_t version_offset = 4;
constexpr std::size_t chain_id_offset = 5;
constexpr std::size_t height_offset = 9;
constexpr std::size_t previous_hash_offset = 17;
constexpr std::size_t transaction_root_offset = 49;
constexpr std::size_t reward_recipient_offset = 81;
constexpr std::size_t timestamp_offset = 113;
constexpr std::size_t transaction_count_offset = 161;

template <typename Range>
void append(crypto::Bytes& output, const Range& range) {
    output.insert(output.end(), range.begin(), range.end());
}

void append_u32_le(crypto::Bytes& output, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

void append_u64_le(crypto::Bytes& output, const std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_u32_le(const crypto::ByteView input, const std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < sizeof(std::uint32_t); ++index) {
        value |= static_cast<std::uint32_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64_le(const crypto::ByteView input, const std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < sizeof(std::uint64_t); ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
std::array<crypto::Byte, Size> read_array(
    const crypto::ByteView input,
    const std::size_t offset
) {
    std::array<crypto::Byte, Size> result{};
    std::ranges::copy(input.subspan(offset, Size), result.begin());
    return result;
}

bool is_zero(const crypto::Hash256& value) noexcept {
    return std::ranges::all_of(value, [](const crypto::Byte byte) { return byte == 0; });
}

crypto::Hash256 hash_leaf(const crypto::Hash256& transaction_id) {
    crypto::Bytes encoded;
    encoded.reserve(1 + transaction_id.size());
    encoded.push_back(merkle_leaf_domain);
    append(encoded, transaction_id);
    return crypto::sha256(encoded);
}

crypto::Hash256 hash_node(
    const crypto::Hash256& left,
    const crypto::Hash256& right
) {
    crypto::Bytes encoded;
    encoded.reserve(1 + left.size() + right.size());
    encoded.push_back(merkle_node_domain);
    append(encoded, left);
    append(encoded, right);
    return crypto::sha256(encoded);
}

BlockError validate_transactions(
    const std::vector<transaction::SignedTransaction>& transactions,
    const std::uint32_t chain_id
) {
    if (transactions.size() > maximum_transactions_per_block) {
        return BlockError::too_many_transactions;
    }

    std::vector<crypto::Hash256> transaction_ids;
    transaction_ids.reserve(transactions.size());
    for (const transaction::SignedTransaction& value : transactions) {
        if (value.chain_id() != chain_id) {
            return BlockError::invalid_transaction;
        }
        const crypto::Hash256 id = value.id();
        if (std::ranges::find(transaction_ids, id) != transaction_ids.end()) {
            return BlockError::duplicate_transaction;
        }
        transaction_ids.push_back(id);
    }
    return BlockError::none;
}

crypto::Hash256 maximum_target() {
    crypto::Hash256 target{};
    target.fill(0xFFU);
    return target;
}

}  // namespace

static_assert(transaction_count_offset + sizeof(std::uint32_t) == block_header_size);

Block::Block(
    const std::uint32_t chain_id,
    const std::uint64_t height,
    crypto::Hash256 previous_block_hash,
    crypto::Hash256 transaction_root,
    std::optional<wallet::Address> reward_recipient,
    const std::uint64_t timestamp,
    crypto::Hash256 difficulty_target,
    const std::uint64_t mining_nonce,
    std::vector<transaction::SignedTransaction> transactions
)
    : chain_id_(chain_id),
      height_(height),
      previous_block_hash_(std::move(previous_block_hash)),
      transaction_root_(std::move(transaction_root)),
      reward_recipient_(std::move(reward_recipient)),
      timestamp_(timestamp),
      difficulty_target_(std::move(difficulty_target)),
      mining_nonce_(mining_nonce),
      transactions_(std::move(transactions)) {}

std::uint32_t Block::chain_id() const noexcept {
    return chain_id_;
}

std::uint64_t Block::height() const noexcept {
    return height_;
}

const crypto::Hash256& Block::previous_block_hash() const noexcept {
    return previous_block_hash_;
}

const crypto::Hash256& Block::transaction_root() const noexcept {
    return transaction_root_;
}

const std::optional<wallet::Address>& Block::reward_recipient() const noexcept {
    return reward_recipient_;
}

std::uint64_t Block::timestamp() const noexcept {
    return timestamp_;
}

const crypto::Hash256& Block::difficulty_target() const noexcept {
    return difficulty_target_;
}

std::uint64_t Block::mining_nonce() const noexcept {
    return mining_nonce_;
}

const std::vector<transaction::SignedTransaction>& Block::transactions() const noexcept {
    return transactions_;
}

bool Block::is_genesis() const noexcept {
    return height_ == 0;
}

crypto::Bytes Block::serialize_header() const {
    crypto::Bytes encoded;
    encoded.reserve(block_header_size);
    append(encoded, block_magic);
    encoded.push_back(block_version);
    append_u32_le(encoded, chain_id_);
    append_u64_le(encoded, height_);
    append(encoded, previous_block_hash_);
    append(encoded, transaction_root_);
    if (reward_recipient_.has_value()) {
        append(encoded, reward_recipient_->hash());
    } else {
        append(encoded, crypto::Hash256{});
    }
    append_u64_le(encoded, timestamp_);
    append(encoded, difficulty_target_);
    append_u64_le(encoded, mining_nonce_);
    append_u32_le(encoded, static_cast<std::uint32_t>(transactions_.size()));
    return encoded;
}

crypto::Bytes Block::serialize() const {
    crypto::Bytes encoded = serialize_header();
    encoded.reserve(
        block_header_size + transactions_.size() * transaction::signed_transaction_size
    );
    for (const transaction::SignedTransaction& value : transactions_) {
        append(encoded, value.serialize());
    }
    return encoded;
}

crypto::Hash256 Block::id() const {
    return crypto::sha256(serialize_header());
}

Block Block::with_mining_nonce(const std::uint64_t mining_nonce) const {
    if (is_genesis()) {
        return *this;
    }
    return Block{
        chain_id_,
        height_,
        previous_block_hash_,
        transaction_root_,
        reward_recipient_,
        timestamp_,
        difficulty_target_,
        mining_nonce,
        transactions_,
    };
}

BlockResult::BlockResult(Block block) : value_(std::move(block)) {}

BlockResult::BlockResult(const BlockError error) : value_(error) {}

bool BlockResult::has_value() const noexcept {
    return std::holds_alternative<Block>(value_);
}

Block& BlockResult::value() & {
    return std::get<Block>(value_);
}

const Block& BlockResult::value() const& {
    return std::get<Block>(value_);
}

Block&& BlockResult::value() && {
    return std::get<Block>(std::move(value_));
}

BlockError BlockResult::error() const noexcept {
    const auto* error = std::get_if<BlockError>(&value_);
    return error == nullptr ? BlockError::none : *error;
}

crypto::Hash256 calculate_transaction_root(
    const std::vector<transaction::SignedTransaction>& transactions
) {
    if (transactions.empty()) {
        const std::array<crypto::Byte, 1> encoded{empty_root_domain};
        return crypto::sha256(encoded);
    }

    std::vector<crypto::Hash256> level;
    level.reserve(transactions.size());
    for (const transaction::SignedTransaction& value : transactions) {
        level.push_back(hash_leaf(value.id()));
    }

    while (level.size() > 1) {
        if (level.size() % 2 != 0) {
            level.push_back(level.back());
        }
        std::vector<crypto::Hash256> next_level;
        next_level.reserve(level.size() / 2);
        for (std::size_t index = 0; index < level.size(); index += 2) {
            next_level.push_back(hash_node(level[index], level[index + 1]));
        }
        level = std::move(next_level);
    }
    return level.front();
}

BlockResult create_block(BlockFields fields) {
    if (core::network_parameters_for_chain(fields.chain_id) == nullptr) {
        return BlockResult{BlockError::unsupported_chain};
    }
    if (fields.height == 0) {
        return BlockResult{BlockError::invalid_height};
    }
    if (is_zero(fields.previous_block_hash)) {
        return BlockResult{BlockError::invalid_previous_hash};
    }
    if (is_zero(fields.reward_recipient.hash())) {
        return BlockResult{BlockError::invalid_reward_recipient};
    }
    const BlockError transaction_error =
        validate_transactions(fields.transactions, fields.chain_id);
    if (transaction_error != BlockError::none) {
        return BlockResult{transaction_error};
    }

    crypto::Hash256 root = calculate_transaction_root(fields.transactions);
    return BlockResult{Block{
        fields.chain_id,
        fields.height,
        std::move(fields.previous_block_hash),
        std::move(root),
        std::move(fields.reward_recipient),
        fields.timestamp,
        std::move(fields.difficulty_target),
        fields.mining_nonce,
        std::move(fields.transactions),
    }};
}

BlockResult deserialize_block(const crypto::ByteView encoded) {
    if (encoded.size() < block_header_size || encoded.size() > maximum_block_size) {
        return BlockResult{BlockError::invalid_size};
    }
    if (!std::ranges::equal(block_magic, encoded.first(block_magic.size()))) {
        return BlockResult{BlockError::invalid_magic};
    }
    if (encoded[version_offset] != block_version) {
        return BlockResult{BlockError::unsupported_version};
    }

    const std::uint32_t chain_id = read_u32_le(encoded, chain_id_offset);
    if (core::network_parameters_for_chain(chain_id) == nullptr) {
        return BlockResult{BlockError::unsupported_chain};
    }
    const std::uint32_t transaction_count =
        read_u32_le(encoded, transaction_count_offset);
    if (transaction_count > maximum_transactions_per_block) {
        return BlockResult{BlockError::too_many_transactions};
    }
    const std::size_t expected_size =
        block_header_size +
        static_cast<std::size_t>(transaction_count) *
            transaction::signed_transaction_size;
    if (encoded.size() != expected_size) {
        return BlockResult{BlockError::invalid_size};
    }

    const std::uint64_t height = read_u64_le(encoded, height_offset);
    crypto::Hash256 previous_hash =
        read_array<crypto::hash256_size>(encoded, previous_hash_offset);
    crypto::Hash256 encoded_root =
        read_array<crypto::hash256_size>(encoded, transaction_root_offset);
    crypto::Hash256 reward_hash =
        read_array<crypto::hash256_size>(encoded, reward_recipient_offset);
    const std::uint64_t timestamp = read_u64_le(encoded, timestamp_offset);
    crypto::Hash256 target =
        read_array<crypto::hash256_size>(encoded, block_difficulty_target_offset);
    const std::uint64_t mining_nonce =
        read_u64_le(encoded, block_mining_nonce_offset);

    std::vector<transaction::SignedTransaction> transactions;
    transactions.reserve(transaction_count);
    std::size_t offset = block_header_size;
    for (std::uint32_t index = 0; index < transaction_count; ++index) {
        transaction::TransactionResult transaction_result =
            transaction::deserialize_transaction(
                encoded.subspan(offset, transaction::signed_transaction_size)
            );
        if (!transaction_result.has_value() ||
            transaction_result.value().chain_id() != chain_id) {
            return BlockResult{BlockError::invalid_transaction};
        }
        transactions.push_back(std::move(transaction_result).value());
        offset += transaction::signed_transaction_size;
    }

    const BlockError transaction_error = validate_transactions(transactions, chain_id);
    if (transaction_error != BlockError::none) {
        return BlockResult{transaction_error};
    }
    if (calculate_transaction_root(transactions) != encoded_root) {
        return BlockResult{BlockError::invalid_transaction_root};
    }

    if (height == 0) {
        Block canonical_genesis = genesis_block(chain_id);
        if (!std::ranges::equal(encoded, canonical_genesis.serialize())) {
            return BlockResult{BlockError::invalid_genesis};
        }
        return BlockResult{std::move(canonical_genesis)};
    }
    if (is_zero(previous_hash)) {
        return BlockResult{BlockError::invalid_previous_hash};
    }
    if (is_zero(reward_hash)) {
        return BlockResult{BlockError::invalid_reward_recipient};
    }

    return BlockResult{Block{
        chain_id,
        height,
        std::move(previous_hash),
        std::move(encoded_root),
        wallet::Address::from_hash(std::move(reward_hash)),
        timestamp,
        std::move(target),
        mining_nonce,
        std::move(transactions),
    }};
}

Block genesis_block(const std::uint32_t chain_id) {
    const std::vector<transaction::SignedTransaction> transactions;
    return Block{
        chain_id,
        0,
        crypto::Hash256{},
        calculate_transaction_root(transactions),
        std::nullopt,
        0,
        maximum_target(),
        0,
        {},
    };
}

std::string_view block_error_message(const BlockError error) noexcept {
    switch (error) {
        case BlockError::none:
            return "no error";
        case BlockError::too_many_transactions:
            return "block contains too many transactions";
        case BlockError::duplicate_transaction:
            return "block contains a duplicate transaction";
        case BlockError::invalid_size:
            return "block size is invalid";
        case BlockError::invalid_magic:
            return "block magic is invalid";
        case BlockError::unsupported_version:
            return "block version is not supported";
        case BlockError::unsupported_chain:
            return "block chain ID is not supported";
        case BlockError::invalid_height:
            return "block height is invalid";
        case BlockError::invalid_previous_hash:
            return "previous block hash is invalid";
        case BlockError::invalid_reward_recipient:
            return "block reward recipient is invalid";
        case BlockError::invalid_transaction:
            return "block contains an invalid transaction";
        case BlockError::invalid_transaction_root:
            return "block transaction root is invalid";
        case BlockError::invalid_genesis:
            return "genesis block does not match the protocol constant";
        case BlockError::file_already_exists:
            return "block file already exists";
        case BlockError::io_error:
            return "block file I/O failed";
    }
    return "unknown block error";
}

}  // namespace bbc::chain
