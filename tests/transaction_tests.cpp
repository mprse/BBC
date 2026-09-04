#include "bbc/crypto/hash.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

bbc::wallet::Wallet deterministic_wallet() {
    constexpr std::string_view private_key_hex =
        "9D61B19DEFFD5A60BA844AF492EC2CC44449C5697B326919703BAC031CAE7F60"
        "D75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A";
    bbc::crypto::Bytes private_key(bbc::crypto::private_key_size);
    REQUIRE(bbc::crypto::decode_hex(private_key_hex, private_key));
    std::optional<bbc::wallet::Wallet> wallet =
        bbc::wallet::Wallet::restore_from_private_key(private_key);
    REQUIRE(wallet.has_value());
    return std::move(*wallet);
}

bbc::wallet::Address deterministic_recipient() {
    bbc::crypto::Hash256 hash{};
    for (std::size_t index = 0; index < hash.size(); ++index) {
        hash[index] = static_cast<bbc::crypto::Byte>(index);
    }
    return bbc::wallet::Address::from_hash(hash);
}

class TemporaryTransactionFile final {
public:
    TemporaryTransactionFile()
        : path_(std::filesystem::temp_directory_path() / "bbc-stage2-test.bbctx") {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryTransactionFile(const TemporaryTransactionFile&) = delete;
    TemporaryTransactionFile& operator=(const TemporaryTransactionFile&) = delete;

    ~TemporaryTransactionFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("transaction version 1 has a deterministic binary encoding", "[transaction]") {
    const bbc::wallet::Wallet signer = deterministic_wallet();
    const bbc::transaction::TransactionFields fields{
        deterministic_recipient(),
        42,
        3,
        7,
    };

    bbc::transaction::TransactionResult result =
        bbc::transaction::sign_transaction(signer, fields);
    REQUIRE(result.has_value());
    const bbc::transaction::SignedTransaction& transaction = result.value();
    const bbc::crypto::Bytes encoded = transaction.serialize();

    REQUIRE(encoded.size() == bbc::transaction::signed_transaction_size);
    CHECK(
        bbc::crypto::to_upper_hex(encoded) ==
        "424254580101000000"
        "D75A980182B10AB7D54BFED3C964073A0EE172F3DAA62325AF021A68F707511A"
        "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"
        "2A00000000000000"
        "0300000000000000"
        "0700000000000000"
        "B49EDB4BA5BD6B7B3ABB9507B5BD9B5D477F173E92CC1CEC1DF46994478E647E"
        "CDF967C6230DDF6C9B10AA4AFF57E9511448214E9D05C6B1426524A65426F60B"
    );
    CHECK(
        bbc::crypto::to_upper_hex(transaction.id()) ==
        "1364586F74E460C7E6090411B46A7A9D0ADA27D5061A11D64C31A76EBC7B2F6F"
    );
}

TEST_CASE("serialized transaction round trip preserves every field", "[transaction]") {
    const bbc::wallet::Wallet signer = deterministic_wallet();
    const bbc::transaction::TransactionFields fields{
        deterministic_recipient(),
        1000,
        5,
        19,
    };
    bbc::transaction::TransactionResult signed_result =
        bbc::transaction::sign_transaction(signer, fields);
    REQUIRE(signed_result.has_value());
    const bbc::crypto::Bytes encoded = signed_result.value().serialize();

    bbc::transaction::TransactionResult decoded_result =
        bbc::transaction::deserialize_transaction(encoded);
    REQUIRE(decoded_result.has_value());
    const bbc::transaction::SignedTransaction& decoded = decoded_result.value();

    CHECK(decoded.sender_public_key() == signer.public_key());
    CHECK(decoded.sender() == signer.address());
    CHECK(decoded.recipient() == fields.recipient);
    CHECK(decoded.amount() == fields.amount);
    CHECK(decoded.fee() == fields.fee);
    CHECK(decoded.account_nonce() == fields.account_nonce);
    CHECK(decoded.id() == signed_result.value().id());
}

TEST_CASE("transaction validation rejects invalid values", "[transaction]") {
    const bbc::wallet::Wallet signer = deterministic_wallet();

    bbc::transaction::TransactionResult zero_amount =
        bbc::transaction::sign_transaction(
            signer,
            {deterministic_recipient(), 0, 0, 0}
        );
    CHECK_FALSE(zero_amount.has_value());
    CHECK(zero_amount.error() == bbc::transaction::TransactionError::zero_amount);

    bbc::transaction::TransactionResult overflow =
        bbc::transaction::sign_transaction(
            signer,
            {
                deterministic_recipient(),
                std::numeric_limits<std::uint64_t>::max(),
                1,
                0,
            }
        );
    CHECK_FALSE(overflow.has_value());
    CHECK(overflow.error() == bbc::transaction::TransactionError::amount_fee_overflow);
}

TEST_CASE("transaction validation rejects modified signed bytes", "[transaction]") {
    const bbc::wallet::Wallet signer = deterministic_wallet();
    bbc::transaction::TransactionResult signed_result =
        bbc::transaction::sign_transaction(
            signer,
            {deterministic_recipient(), 10, 1, 0}
        );
    REQUIRE(signed_result.has_value());
    bbc::crypto::Bytes encoded = signed_result.value().serialize();

    encoded[41] ^= 0x01;
    bbc::transaction::TransactionResult modified =
        bbc::transaction::deserialize_transaction(encoded);

    CHECK_FALSE(modified.has_value());
    CHECK(modified.error() == bbc::transaction::TransactionError::invalid_signature);
}

TEST_CASE("transaction decoder rejects invalid protocol fields", "[transaction]") {
    const bbc::wallet::Wallet signer = deterministic_wallet();
    bbc::transaction::TransactionResult signed_result =
        bbc::transaction::sign_transaction(
            signer,
            {deterministic_recipient(), 10, 1, 0}
        );
    REQUIRE(signed_result.has_value());
    const bbc::crypto::Bytes valid = signed_result.value().serialize();

    bbc::crypto::Bytes short_transaction = valid;
    short_transaction.pop_back();
    CHECK(
        bbc::transaction::deserialize_transaction(short_transaction).error() ==
        bbc::transaction::TransactionError::invalid_size
    );

    bbc::crypto::Bytes invalid_magic = valid;
    invalid_magic[0] ^= 0x01;
    CHECK(
        bbc::transaction::deserialize_transaction(invalid_magic).error() ==
        bbc::transaction::TransactionError::invalid_magic
    );

    bbc::crypto::Bytes unsupported_version = valid;
    unsupported_version[4] = 2;
    CHECK(
        bbc::transaction::deserialize_transaction(unsupported_version).error() ==
        bbc::transaction::TransactionError::unsupported_version
    );

    bbc::crypto::Bytes unsupported_chain = valid;
    unsupported_chain[5] = 3;
    CHECK(
        bbc::transaction::deserialize_transaction(unsupported_chain).error() ==
        bbc::transaction::TransactionError::unsupported_chain
    );
}

TEST_CASE("transaction files round trip and are never overwritten", "[transaction]") {
    TemporaryTransactionFile file;
    const bbc::wallet::Wallet signer = deterministic_wallet();
    bbc::transaction::TransactionResult signed_result =
        bbc::transaction::sign_transaction(
            signer,
            {deterministic_recipient(), 25, 2, 4}
        );
    REQUIRE(signed_result.has_value());

    REQUIRE(
        bbc::transaction::save_transaction(signed_result.value(), file.path()) ==
        bbc::transaction::TransactionError::none
    );
    CHECK(std::filesystem::file_size(file.path()) ==
          bbc::transaction::signed_transaction_size);
    CHECK(
        bbc::transaction::save_transaction(signed_result.value(), file.path()) ==
        bbc::transaction::TransactionError::file_already_exists
    );

    bbc::transaction::TransactionResult loaded =
        bbc::transaction::load_transaction(file.path());
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().id() == signed_result.value().id());
}
