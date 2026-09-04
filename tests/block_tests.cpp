#include "bbc/app/application.hpp"
#include "bbc/chain/block.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

bbc::wallet::Address address_with_first_byte(const bbc::crypto::Byte value) {
    bbc::crypto::Hash256 hash{};
    hash.front() = value;
    return bbc::wallet::Address::from_hash(hash);
}

bbc::wallet::Address deterministic_recipient() {
    bbc::crypto::Hash256 hash{};
    for (std::size_t index = 0; index < hash.size(); ++index) {
        hash[index] = static_cast<bbc::crypto::Byte>(index);
    }
    return bbc::wallet::Address::from_hash(hash);
}

bbc::transaction::SignedTransaction deterministic_transaction(
    const std::uint64_t amount = 42,
    const std::uint64_t account_nonce = 7
) {
    const bbc::wallet::Wallet signer = deterministic_wallet();
    bbc::transaction::TransactionResult result =
        bbc::transaction::sign_transaction(
            signer,
            {deterministic_recipient(), amount, 3, account_nonce}
        );
    REQUIRE(result.has_value());
    return std::move(result).value();
}

bbc::crypto::Hash256 maximum_target() {
    bbc::crypto::Hash256 target{};
    target.fill(0xFFU);
    return target;
}

bbc::chain::BlockResult create_test_block(
    std::vector<bbc::transaction::SignedTransaction> transactions = {
        deterministic_transaction()
    }
) {
    return bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        address_with_first_byte(0x44),
        1'788'393'600,
        maximum_target(),
        7,
        std::move(transactions),
    });
}

class TemporaryBlockFile final {
public:
    TemporaryBlockFile()
        : path_(std::filesystem::temp_directory_path() / "bbc-stage3-test.bbcblock") {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryBlockFile(const TemporaryBlockFile&) = delete;
    TemporaryBlockFile& operator=(const TemporaryBlockFile&) = delete;

    ~TemporaryBlockFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class TemporaryBlockDirectory final {
public:
    TemporaryBlockDirectory()
        : path_(std::filesystem::temp_directory_path() / "bbc-stage3-cli-test") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    TemporaryBlockDirectory(const TemporaryBlockDirectory&) = delete;
    TemporaryBlockDirectory& operator=(const TemporaryBlockDirectory&) = delete;

    ~TemporaryBlockDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path file(const std::string_view name) const {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("genesis block is a deterministic protocol constant", "[block]") {
    const bbc::chain::Block genesis = bbc::chain::genesis_block();
    const bbc::crypto::Bytes encoded = genesis.serialize();

    CHECK(genesis.is_genesis());
    CHECK(genesis.height() == 0);
    CHECK_FALSE(genesis.reward_recipient().has_value());
    CHECK(genesis.transactions().empty());
    CHECK(encoded.size() == bbc::chain::block_header_size);
    CHECK(encoded[0] == 'B');
    CHECK(encoded[1] == 'B');
    CHECK(encoded[2] == 'L');
    CHECK(encoded[3] == 'K');
    CHECK(genesis.with_mining_nonce(1).id() == genesis.id());
    CHECK(
        bbc::crypto::to_upper_hex(genesis.id()) ==
        "10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2"
    );

    bbc::chain::BlockResult decoded = bbc::chain::deserialize_block(encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().id() == genesis.id());
}

TEST_CASE("block version 1 has a deterministic binary header", "[block]") {
    bbc::chain::BlockResult created = create_test_block();
    REQUIRE(created.has_value());

    CHECK(
        bbc::crypto::to_upper_hex(created.value().serialize_header()) ==
        "42424C4B01"
        "01000000"
        "0100000000000000"
        "10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2"
        "4CDC0CB757A3C3DAC9812CB8BCEE9B57BB44172A2F49F872C22C664979DEB083"
        "4400000000000000000000000000000000000000000000000000000000000000"
        "80B8986A00000000"
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        "0700000000000000"
        "01000000"
    );
    CHECK(
        bbc::crypto::to_upper_hex(created.value().id()) ==
        "87800A173712A8C6E7988BE497477E1F9C92355E72B0FC8CEC9BAD074D46A64D"
    );
}

TEST_CASE("block serialization round trip preserves every field", "[block]") {
    bbc::chain::BlockResult created = create_test_block();
    REQUIRE(created.has_value());
    const bbc::crypto::Bytes encoded = created.value().serialize();

    REQUIRE(
        encoded.size() ==
        bbc::chain::block_header_size + bbc::transaction::signed_transaction_size
    );
    bbc::chain::BlockResult decoded = bbc::chain::deserialize_block(encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().id() == created.value().id());
    CHECK(decoded.value().height() == 1);
    CHECK(decoded.value().previous_block_hash() == bbc::chain::genesis_block().id());
    REQUIRE(decoded.value().reward_recipient().has_value());
    CHECK(*decoded.value().reward_recipient() == address_with_first_byte(0x44));
    CHECK(decoded.value().timestamp() == 1'788'393'600);
    CHECK(decoded.value().difficulty_target() == maximum_target());
    CHECK(decoded.value().mining_nonce() == 7);
    REQUIRE(decoded.value().transactions().size() == 1);
    CHECK(
        decoded.value().transactions().front().id() ==
        created.value().transactions().front().id()
    );
}

TEST_CASE("transaction root commits to transaction order", "[block]") {
    std::vector<bbc::transaction::SignedTransaction> first_order;
    first_order.push_back(deterministic_transaction(10, 0));
    first_order.push_back(deterministic_transaction(20, 1));
    std::vector<bbc::transaction::SignedTransaction> second_order{
        first_order[1],
        first_order[0],
    };

    CHECK(
        bbc::chain::calculate_transaction_root(first_order) !=
        bbc::chain::calculate_transaction_root(second_order)
    );
}

TEST_CASE("block creation rejects invalid structural fields", "[block]") {
    bbc::crypto::Hash256 zero_hash{};
    std::vector<bbc::transaction::SignedTransaction> duplicate_transactions{
        deterministic_transaction(),
        deterministic_transaction(),
    };

    CHECK(
        bbc::chain::create_block({
            0,
            bbc::chain::genesis_block().id(),
            address_with_first_byte(0x44),
            1,
            maximum_target(),
            0,
            {},
        }).error() == bbc::chain::BlockError::invalid_height
    );
    CHECK(
        bbc::chain::create_block({
            1,
            zero_hash,
            address_with_first_byte(0x44),
            1,
            maximum_target(),
            0,
            {},
        }).error() == bbc::chain::BlockError::invalid_previous_hash
    );
    CHECK(
        bbc::chain::create_block({
            1,
            bbc::chain::genesis_block().id(),
            bbc::wallet::Address::from_hash(zero_hash),
            1,
            maximum_target(),
            0,
            {},
        }).error() == bbc::chain::BlockError::invalid_reward_recipient
    );
    CHECK(
        create_test_block(std::move(duplicate_transactions)).error() ==
        bbc::chain::BlockError::duplicate_transaction
    );

    std::vector<bbc::transaction::SignedTransaction> too_many;
    too_many.reserve(bbc::chain::maximum_transactions_per_block + 1);
    const bbc::transaction::SignedTransaction value = deterministic_transaction();
    for (std::size_t index = 0;
         index <= bbc::chain::maximum_transactions_per_block;
         ++index) {
        too_many.push_back(value);
    }
    CHECK(
        create_test_block(std::move(too_many)).error() ==
        bbc::chain::BlockError::too_many_transactions
    );
}

TEST_CASE("block decoder rejects modified and malformed data", "[block]") {
    bbc::chain::BlockResult created = create_test_block();
    REQUIRE(created.has_value());
    const bbc::crypto::Bytes valid = created.value().serialize();

    bbc::crypto::Bytes short_block = valid;
    short_block.pop_back();
    CHECK(
        bbc::chain::deserialize_block(short_block).error() ==
        bbc::chain::BlockError::invalid_size
    );

    bbc::crypto::Bytes invalid_magic = valid;
    invalid_magic[0] ^= 0x01;
    CHECK(
        bbc::chain::deserialize_block(invalid_magic).error() ==
        bbc::chain::BlockError::invalid_magic
    );

    bbc::crypto::Bytes unsupported_version = valid;
    unsupported_version[4] = 2;
    CHECK(
        bbc::chain::deserialize_block(unsupported_version).error() ==
        bbc::chain::BlockError::unsupported_version
    );

    bbc::crypto::Bytes unsupported_chain = valid;
    unsupported_chain[5] = 2;
    CHECK(
        bbc::chain::deserialize_block(unsupported_chain).error() ==
        bbc::chain::BlockError::unsupported_chain
    );

    bbc::crypto::Bytes invalid_root = valid;
    invalid_root[49] ^= 0x01;
    CHECK(
        bbc::chain::deserialize_block(invalid_root).error() ==
        bbc::chain::BlockError::invalid_transaction_root
    );

    bbc::crypto::Bytes invalid_transaction = valid;
    invalid_transaction[bbc::chain::block_header_size + 41] ^= 0x01;
    CHECK(
        bbc::chain::deserialize_block(invalid_transaction).error() ==
        bbc::chain::BlockError::invalid_transaction
    );

    bbc::crypto::Bytes invalid_genesis = bbc::chain::genesis_block().serialize();
    invalid_genesis[113] = 1;
    CHECK(
        bbc::chain::deserialize_block(invalid_genesis).error() ==
        bbc::chain::BlockError::invalid_genesis
    );
}

TEST_CASE("block files round trip and are never overwritten", "[block]") {
    TemporaryBlockFile file;
    bbc::chain::BlockResult created = create_test_block();
    REQUIRE(created.has_value());

    REQUIRE(
        bbc::chain::save_block(created.value(), file.path()) ==
        bbc::chain::BlockError::none
    );
    CHECK(
        std::filesystem::file_size(file.path()) ==
        bbc::chain::block_header_size + bbc::transaction::signed_transaction_size
    );
    CHECK(
        bbc::chain::save_block(created.value(), file.path()) ==
        bbc::chain::BlockError::file_already_exists
    );

    bbc::chain::BlockResult loaded = bbc::chain::load_block(file.path());
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().id() == created.value().id());
}

TEST_CASE("block commands create show and verify binary files", "[application][block]") {
    TemporaryBlockDirectory directory;
    const std::string genesis_path = directory.file("genesis.bbcblock").string();
    const std::array<std::string_view, 4> genesis_arguments{
        "block", "genesis", "--out", genesis_path
    };
    std::ostringstream genesis_output;
    std::ostringstream error_output;

    REQUIRE(
        bbc::app::run(genesis_arguments, genesis_output, error_output) == 0
    );
    CHECK(std::filesystem::file_size(genesis_path) == bbc::chain::block_header_size);
    CHECK(
        genesis_output.str().find(
            "Block ID: 10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2"
        ) != std::string::npos
    );

    const std::string block_path = directory.file("block-1.bbcblock").string();
    constexpr std::string_view genesis_id =
        "10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2";
    constexpr std::string_view reward_address =
        "BBC_4400000000000000000000000000000000000000000000000000000000000000";
    const std::array<std::string_view, 14> create_arguments{
        "block",
        "create",
        "--height",
        "1",
        "--previous",
        genesis_id,
        "--reward-to",
        reward_address,
        "--timestamp",
        "1788393600",
        "--nonce",
        "7707263",
        "--out",
        block_path,
    };
    std::ostringstream create_output;
    REQUIRE(bbc::app::run(create_arguments, create_output, error_output) == 0);
    CHECK(create_output.str().find("Transaction count: 0") != std::string::npos);

    const std::array<std::string_view, 4> show_arguments{
        "block", "show", "--file", block_path
    };
    std::ostringstream show_output;
    REQUIRE(bbc::app::run(show_arguments, show_output, error_output) == 0);
    CHECK(show_output.str().find(std::string{reward_address}) != std::string::npos);

    const std::array<std::string_view, 4> verify_arguments{
        "block", "verify", "--file", block_path
    };
    std::ostringstream verify_output;
    CHECK(bbc::app::run(verify_arguments, verify_output, error_output) == 0);
    CHECK(
        verify_output.str().find("Block verification: success") !=
        std::string::npos
    );

    const std::string mined_path = directory.file("mined.bbcblock").string();
    const std::array<std::string_view, 8> mine_arguments{
        "block",
        "mine",
        "--file",
        block_path,
        "--out",
        mined_path,
        "--max-attempts",
        "1",
    };
    std::ostringstream mine_output;
    REQUIRE(bbc::app::run(mine_arguments, mine_output, error_output) == 0);
    CHECK(mine_output.str().find("Attempts: 1") != std::string::npos);
    CHECK(std::filesystem::is_regular_file(mined_path));
    CHECK(error_output.str().empty());

    std::ostringstream duplicate_output;
    std::ostringstream duplicate_error;
    CHECK(
        bbc::app::run(mine_arguments, duplicate_output, duplicate_error) == 1
    );
    CHECK(duplicate_output.str().empty());
    CHECK(duplicate_error.str().find("block file already exists") != std::string::npos);
}

TEST_CASE("block create accepts repeated transaction files", "[application][block]") {
    TemporaryBlockDirectory directory;
    const std::filesystem::path first_path = directory.file("first.bbctx");
    const std::filesystem::path second_path = directory.file("second.bbctx");
    REQUIRE(
        bbc::transaction::save_transaction(
            deterministic_transaction(10, 0),
            first_path
        ) == bbc::transaction::TransactionError::none
    );
    REQUIRE(
        bbc::transaction::save_transaction(
            deterministic_transaction(20, 1),
            second_path
        ) == bbc::transaction::TransactionError::none
    );

    const std::string first = first_path.string();
    const std::string second = second_path.string();
    const std::string block_path = directory.file("transactions.bbcblock").string();
    constexpr std::string_view genesis_id =
        "10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2";
    constexpr std::string_view reward_address =
        "BBC_4400000000000000000000000000000000000000000000000000000000000000";
    const std::array<std::string_view, 16> arguments{
        "block",
        "create",
        "--height",
        "1",
        "--previous",
        genesis_id,
        "--reward-to",
        reward_address,
        "--timestamp",
        "1788393600",
        "--transaction",
        first,
        "--transaction",
        second,
        "--out",
        block_path,
    };
    std::ostringstream output;
    std::ostringstream error_output;

    REQUIRE(bbc::app::run(arguments, output, error_output) == 0);
    CHECK(output.str().find("Transaction count: 2") != std::string::npos);
    bbc::chain::BlockResult loaded = bbc::chain::load_block(block_path);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().transactions().size() == 2);
    CHECK(error_output.str().empty());
}
