#pragma once

#include "bbc/crypto/types.hpp"
#include "bbc/wallet/address.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <variant>

namespace bbc::wallet {
class Wallet;
}

namespace bbc::transaction {

inline constexpr std::uint32_t current_chain_id = 1;
inline constexpr std::size_t unsigned_transaction_size = 97;
inline constexpr std::size_t signed_transaction_size = 161;

enum class TransactionError {
    none,
    zero_amount,
    amount_fee_overflow,
    invalid_size,
    invalid_magic,
    unsupported_version,
    unsupported_chain,
    invalid_signature,
    file_already_exists,
    io_error,
};

struct TransactionFields {
    wallet::Address recipient;
    std::uint64_t amount;
    std::uint64_t fee;
    std::uint64_t account_nonce;
    std::uint32_t chain_id = current_chain_id;
};

class TransactionResult;

class SignedTransaction final {
public:
    [[nodiscard]] std::uint32_t chain_id() const noexcept;
    [[nodiscard]] const crypto::PublicKey& sender_public_key() const noexcept;
    [[nodiscard]] wallet::Address sender() const;
    [[nodiscard]] const wallet::Address& recipient() const noexcept;
    [[nodiscard]] std::uint64_t amount() const noexcept;
    [[nodiscard]] std::uint64_t fee() const noexcept;
    [[nodiscard]] std::uint64_t account_nonce() const noexcept;
    [[nodiscard]] const crypto::Signature& signature() const noexcept;

    [[nodiscard]] crypto::Bytes serialize() const;
    [[nodiscard]] crypto::Hash256 id() const;

private:
    friend TransactionResult sign_transaction(
        const wallet::Wallet& signer,
        const TransactionFields& fields
    );
    friend TransactionResult deserialize_transaction(crypto::ByteView encoded);

    SignedTransaction(
        std::uint32_t chain_id,
        crypto::PublicKey sender_public_key,
        wallet::Address recipient,
        std::uint64_t amount,
        std::uint64_t fee,
        std::uint64_t account_nonce,
        crypto::Signature signature
    );

    [[nodiscard]] crypto::Bytes signing_bytes() const;

    std::uint32_t chain_id_;
    crypto::PublicKey sender_public_key_{};
    wallet::Address recipient_;
    std::uint64_t amount_;
    std::uint64_t fee_;
    std::uint64_t account_nonce_;
    crypto::Signature signature_{};
};

class TransactionResult final {
public:
    explicit TransactionResult(SignedTransaction transaction);
    explicit TransactionResult(TransactionError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] SignedTransaction& value() &;
    [[nodiscard]] const SignedTransaction& value() const&;
    [[nodiscard]] SignedTransaction&& value() &&;
    [[nodiscard]] TransactionError error() const noexcept;

private:
    std::variant<SignedTransaction, TransactionError> value_;
};

[[nodiscard]] TransactionResult sign_transaction(
    const wallet::Wallet& signer,
    const TransactionFields& fields
);

[[nodiscard]] TransactionResult deserialize_transaction(crypto::ByteView encoded);

[[nodiscard]] TransactionError save_transaction(
    const SignedTransaction& transaction,
    const std::filesystem::path& path
);

[[nodiscard]] TransactionResult load_transaction(const std::filesystem::path& path);

[[nodiscard]] std::string_view transaction_error_message(TransactionError error) noexcept;

}  // namespace bbc::transaction
