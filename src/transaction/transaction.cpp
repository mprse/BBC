#include "bbc/transaction/transaction.hpp"

#include "bbc/core/network.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/crypto/keys.hpp"
#include "bbc/wallet/wallet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace bbc::transaction {
namespace {

constexpr std::array<crypto::Byte, 4> transaction_magic{'B', 'B', 'T', 'X'};
constexpr crypto::Byte transaction_version = 1;

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

TransactionError validate_fields(const TransactionFields& fields) noexcept {
    if (core::network_parameters_for_chain(fields.chain_id) == nullptr) {
        return TransactionError::unsupported_chain;
    }
    if (fields.amount == 0) {
        return TransactionError::zero_amount;
    }
    if (fields.fee > std::numeric_limits<std::uint64_t>::max() - fields.amount) {
        return TransactionError::amount_fee_overflow;
    }
    return TransactionError::none;
}

crypto::Bytes serialize_unsigned(
    const std::uint32_t chain_id,
    const crypto::PublicKey& sender_public_key,
    const wallet::Address& recipient,
    const std::uint64_t amount,
    const std::uint64_t fee,
    const std::uint64_t account_nonce
) {
    crypto::Bytes encoded;
    encoded.reserve(unsigned_transaction_size);
    append(encoded, transaction_magic);
    encoded.push_back(transaction_version);
    append_u32_le(encoded, chain_id);
    append(encoded, sender_public_key);
    append(encoded, recipient.hash());
    append_u64_le(encoded, amount);
    append_u64_le(encoded, fee);
    append_u64_le(encoded, account_nonce);
    return encoded;
}

}  // namespace

SignedTransaction::SignedTransaction(
    const std::uint32_t chain_id,
    crypto::PublicKey sender_public_key,
    wallet::Address recipient,
    const std::uint64_t amount,
    const std::uint64_t fee,
    const std::uint64_t account_nonce,
    crypto::Signature signature
)
    : chain_id_(chain_id),
      sender_public_key_(sender_public_key),
      recipient_(std::move(recipient)),
      amount_(amount),
      fee_(fee),
      account_nonce_(account_nonce),
      signature_(signature) {}

std::uint32_t SignedTransaction::chain_id() const noexcept {
    return chain_id_;
}

const crypto::PublicKey& SignedTransaction::sender_public_key() const noexcept {
    return sender_public_key_;
}

wallet::Address SignedTransaction::sender() const {
    return wallet::Address::from_public_key(sender_public_key_);
}

const wallet::Address& SignedTransaction::recipient() const noexcept {
    return recipient_;
}

std::uint64_t SignedTransaction::amount() const noexcept {
    return amount_;
}

std::uint64_t SignedTransaction::fee() const noexcept {
    return fee_;
}

std::uint64_t SignedTransaction::account_nonce() const noexcept {
    return account_nonce_;
}

const crypto::Signature& SignedTransaction::signature() const noexcept {
    return signature_;
}

crypto::Bytes SignedTransaction::signing_bytes() const {
    return serialize_unsigned(
        chain_id_,
        sender_public_key_,
        recipient_,
        amount_,
        fee_,
        account_nonce_
    );
}

crypto::Bytes SignedTransaction::serialize() const {
    crypto::Bytes encoded = signing_bytes();
    encoded.reserve(signed_transaction_size);
    append(encoded, signature_);
    return encoded;
}

crypto::Hash256 SignedTransaction::id() const {
    return crypto::sha256(serialize());
}

TransactionResult::TransactionResult(SignedTransaction transaction)
    : value_(std::move(transaction)) {}

TransactionResult::TransactionResult(const TransactionError error) : value_(error) {}

bool TransactionResult::has_value() const noexcept {
    return std::holds_alternative<SignedTransaction>(value_);
}

SignedTransaction& TransactionResult::value() & {
    return std::get<SignedTransaction>(value_);
}

const SignedTransaction& TransactionResult::value() const& {
    return std::get<SignedTransaction>(value_);
}

SignedTransaction&& TransactionResult::value() && {
    return std::get<SignedTransaction>(std::move(value_));
}

TransactionError TransactionResult::error() const noexcept {
    const auto* error = std::get_if<TransactionError>(&value_);
    return error == nullptr ? TransactionError::none : *error;
}

TransactionResult sign_transaction(
    const wallet::Wallet& signer,
    const TransactionFields& fields
) {
    const TransactionError validation_error = validate_fields(fields);
    if (validation_error != TransactionError::none) {
        return TransactionResult{validation_error};
    }

    const crypto::Bytes signing_bytes = serialize_unsigned(
        fields.chain_id,
        signer.public_key(),
        fields.recipient,
        fields.amount,
        fields.fee,
        fields.account_nonce
    );
    crypto::Signature signature = signer.sign(signing_bytes);
    return TransactionResult{SignedTransaction{
        fields.chain_id,
        signer.public_key(),
        fields.recipient,
        fields.amount,
        fields.fee,
        fields.account_nonce,
        std::move(signature),
    }};
}

TransactionResult deserialize_transaction(const crypto::ByteView encoded) {
    if (encoded.size() != signed_transaction_size) {
        return TransactionResult{TransactionError::invalid_size};
    }
    if (!std::ranges::equal(transaction_magic, encoded.first(transaction_magic.size()))) {
        return TransactionResult{TransactionError::invalid_magic};
    }

    std::size_t offset = transaction_magic.size();
    if (encoded[offset++] != transaction_version) {
        return TransactionResult{TransactionError::unsupported_version};
    }

    const std::uint32_t chain_id = read_u32_le(encoded, offset);
    offset += sizeof(std::uint32_t);
    if (core::network_parameters_for_chain(chain_id) == nullptr) {
        return TransactionResult{TransactionError::unsupported_chain};
    }

    crypto::PublicKey sender_public_key{};
    std::ranges::copy(
        encoded.subspan(offset, sender_public_key.size()),
        sender_public_key.begin()
    );
    offset += sender_public_key.size();

    crypto::Hash256 recipient_hash{};
    std::ranges::copy(
        encoded.subspan(offset, recipient_hash.size()),
        recipient_hash.begin()
    );
    offset += recipient_hash.size();
    wallet::Address recipient = wallet::Address::from_hash(std::move(recipient_hash));

    const std::uint64_t amount = read_u64_le(encoded, offset);
    offset += sizeof(std::uint64_t);
    const std::uint64_t fee = read_u64_le(encoded, offset);
    offset += sizeof(std::uint64_t);
    const std::uint64_t account_nonce = read_u64_le(encoded, offset);
    offset += sizeof(std::uint64_t);

    TransactionFields fields{recipient, amount, fee, account_nonce, chain_id};
    const TransactionError validation_error = validate_fields(fields);
    if (validation_error != TransactionError::none) {
        return TransactionResult{validation_error};
    }

    crypto::Signature signature{};
    std::ranges::copy(encoded.subspan(offset, signature.size()), signature.begin());
    if (!crypto::verify_signature(
            encoded.first(unsigned_transaction_size),
            signature,
            sender_public_key
        )) {
        return TransactionResult{TransactionError::invalid_signature};
    }

    return TransactionResult{SignedTransaction{
        chain_id,
        std::move(sender_public_key),
        std::move(recipient),
        amount,
        fee,
        account_nonce,
        std::move(signature),
    }};
}

std::string_view transaction_error_message(const TransactionError error) noexcept {
    switch (error) {
        case TransactionError::none:
            return "no error";
        case TransactionError::zero_amount:
            return "transaction amount must be greater than zero";
        case TransactionError::amount_fee_overflow:
            return "transaction amount plus fee overflows uint64";
        case TransactionError::invalid_size:
            return "transaction size is invalid";
        case TransactionError::invalid_magic:
            return "transaction magic is invalid";
        case TransactionError::unsupported_version:
            return "transaction version is not supported";
        case TransactionError::unsupported_chain:
            return "transaction chain ID is not supported";
        case TransactionError::invalid_signature:
            return "transaction signature is invalid";
        case TransactionError::file_already_exists:
            return "transaction file already exists";
        case TransactionError::io_error:
            return "transaction file I/O failed";
    }
    return "unknown transaction error";
}

}  // namespace bbc::transaction
