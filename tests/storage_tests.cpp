#include "bbc/app/application.hpp"
#include "bbc/chain/block.hpp"
#include "bbc/chain/state.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/storage/chain_store.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

bbc::wallet::Address reward_address() {
    bbc::crypto::Hash256 hash{};
    hash.front() = 0x44;
    return bbc::wallet::Address::from_hash(hash);
}

bbc::chain::Block known_mined_block_one() {
    bbc::chain::BlockResult result = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        reward_address(),
        1'788'393'600,
        bbc::consensus::fixed_difficulty_target(),
        7'707'263,
        {},
    });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

class TemporaryDataDirectory final {
public:
    explicit TemporaryDataDirectory(const std::string_view name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDataDirectory(const TemporaryDataDirectory&) = delete;
    TemporaryDataDirectory& operator=(const TemporaryDataDirectory&) = delete;

    ~TemporaryDataDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class TemporaryBlockFile final {
public:
    explicit TemporaryBlockFile(const std::string_view name)
        : path_(std::filesystem::temp_directory_path() / name) {
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

class TemporaryWalletFile final {
public:
    explicit TemporaryWalletFile(const std::string_view name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryWalletFile(const TemporaryWalletFile&) = delete;
    TemporaryWalletFile& operator=(const TemporaryWalletFile&) = delete;

    ~TemporaryWalletFile() {
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

TEST_CASE("chain store initialization persists canonical Genesis", "[storage]") {
    TemporaryDataDirectory directory{"bbc-stage5-1-initialize"};
    bbc::storage::ChainStoreResult initialized =
        bbc::storage::ChainStore::initialize(directory.path());

    REQUIRE(initialized.has_value());
    CHECK(std::filesystem::is_regular_file(initialized.value().blocks_path()));
    CHECK(std::filesystem::is_regular_file(initialized.value().database_path()));
    CHECK(initialized.value().blockchain().block_count() == 1);
    CHECK(
        initialized.value().blockchain().tip().id() ==
        bbc::chain::genesis_block().id()
    );
    CHECK(
        bbc::storage::ChainStore::initialize(directory.path()).error() ==
        bbc::storage::ChainStoreError::already_initialized
    );
}

TEST_CASE("chain store append survives close and reopen", "[storage][state]") {
    TemporaryDataDirectory directory{"bbc-stage5-1-reopen"};
    bbc::storage::ChainStoreResult initialized =
        bbc::storage::ChainStore::initialize(directory.path());
    REQUIRE(initialized.has_value());
    const std::uintmax_t original_size =
        std::filesystem::file_size(initialized.value().blocks_path());

    const bbc::storage::ChainStoreAppendResult appended =
        initialized.value().append(known_mined_block_one());
    REQUIRE(appended.has_value());
    CHECK(std::filesystem::file_size(initialized.value().blocks_path()) > original_size);
    const std::uintmax_t appended_size =
        std::filesystem::file_size(initialized.value().blocks_path());
    const bbc::storage::ChainStoreAppendResult duplicate =
        initialized.value().append(known_mined_block_one());
    CHECK_FALSE(duplicate.has_value());
    CHECK(duplicate.chain_result.error == bbc::chain::ChainError::unexpected_height);
    CHECK(std::filesystem::file_size(initialized.value().blocks_path()) == appended_size);

    bbc::storage::ChainStoreResult reopened =
        bbc::storage::ChainStore::open(directory.path());
    REQUIRE(reopened.has_value());
    CHECK(reopened.value().blockchain().tip().height() == 1);
    CHECK(
        reopened.value().blockchain().state().account(reward_address()).balance ==
        bbc::chain::block_subsidy
    );
}

TEST_CASE("rejected block never changes persistent history", "[storage]") {
    TemporaryDataDirectory directory{"bbc-stage5-1-rejected"};
    bbc::storage::ChainStoreResult store =
        bbc::storage::ChainStore::initialize(directory.path());
    REQUIRE(store.has_value());
    const std::uintmax_t original_size =
        std::filesystem::file_size(store.value().blocks_path());

    const bbc::storage::ChainStoreAppendResult rejected =
        store.value().append(known_mined_block_one().with_mining_nonce(0));

    CHECK_FALSE(rejected.has_value());
    CHECK(
        rejected.chain_result.error ==
        bbc::chain::ChainError::invalid_proof_of_work
    );
    CHECK(std::filesystem::file_size(store.value().blocks_path()) == original_size);
    CHECK(store.value().blockchain().tip().height() == 0);
}

TEST_CASE("missing SQLite cache is rebuilt from blocks dat", "[storage]") {
    TemporaryDataDirectory directory{"bbc-stage5-1-rebuild"};
    bbc::storage::ChainStoreResult store =
        bbc::storage::ChainStore::initialize(directory.path());
    REQUIRE(store.has_value());
    REQUIRE(store.value().append(known_mined_block_one()).has_value());
    const std::filesystem::path database_path = store.value().database_path();
    std::error_code error;
    REQUIRE(std::filesystem::remove(database_path, error));
    REQUIRE_FALSE(error);

    bbc::storage::ChainStoreResult rebuilt =
        bbc::storage::ChainStore::open(directory.path());

    REQUIRE(rebuilt.has_value());
    CHECK(std::filesystem::is_regular_file(database_path));
    CHECK(rebuilt.value().blockchain().tip().height() == 1);
    CHECK(
        rebuilt.value().blockchain().state().account(reward_address()).balance ==
        bbc::chain::block_subsidy
    );
}

TEST_CASE("chain store detects truncated and corrupted records", "[storage]") {
    SECTION("truncated record") {
        TemporaryDataDirectory directory{"bbc-stage5-1-truncated"};
        bbc::storage::ChainStoreResult store =
            bbc::storage::ChainStore::initialize(directory.path());
        REQUIRE(store.has_value());
        const std::uintmax_t size =
            std::filesystem::file_size(store.value().blocks_path());
        std::filesystem::resize_file(store.value().blocks_path(), size - 1);

        CHECK(
            bbc::storage::ChainStore::open(directory.path()).error() ==
            bbc::storage::ChainStoreError::invalid_record_size
        );
    }

    SECTION("checksum mismatch") {
        TemporaryDataDirectory directory{"bbc-stage5-1-corrupt"};
        bbc::storage::ChainStoreResult store =
            bbc::storage::ChainStore::initialize(directory.path());
        REQUIRE(store.has_value());
        std::fstream file(
            store.value().blocks_path(),
            std::ios::binary | std::ios::in | std::ios::out
        );
        REQUIRE(file);
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        file.clear();
        file.seekp(-1, std::ios::end);
        byte ^= 0x01;
        file.write(&byte, 1);
        file.close();

        CHECK(
            bbc::storage::ChainStore::open(directory.path()).error() ==
            bbc::storage::ChainStoreError::invalid_record_checksum
        );
    }
}

TEST_CASE(
    "chain store CLI initializes adds and queries persistent state",
    "[application][storage]"
) {
    TemporaryDataDirectory directory{"bbc-stage5-1-cli"};
    TemporaryBlockFile block_file{"bbc-stage5-1-cli.bbcblock"};
    REQUIRE(
        bbc::chain::save_block(known_mined_block_one(), block_file.path()) ==
        bbc::chain::BlockError::none
    );
    const std::string data_path = directory.path().string();
    const std::string block_path = block_file.path().string();
    const std::string address{reward_address().value()};
    std::ostringstream output;
    std::ostringstream error_output;

    const std::array<std::string_view, 4> init_arguments{
        "chain", "init", "--data-dir", data_path
    };
    REQUIRE(bbc::app::run(init_arguments, output, error_output) == 0);

    const std::array<std::string_view, 6> add_arguments{
        "chain", "add", "--data-dir", data_path, "--block", block_path
    };
    REQUIRE(bbc::app::run(add_arguments, output, error_output) == 0);

    const std::array<std::string_view, 6> balance_arguments{
        "chain", "balance", "--address", address, "--data-dir", data_path
    };
    REQUIRE(bbc::app::run(balance_arguments, output, error_output) == 0);
    CHECK(output.str().find("Balance (base units): 5000000000") != std::string::npos);
    CHECK(output.str().find("Chain height: 1") != std::string::npos);
    CHECK(error_output.str().empty());
}

TEST_CASE("wallet balance reads a persistent chain store", "[application][storage]") {
    TemporaryDataDirectory directory{"bbc-stage5-1-wallet-cli"};
    TemporaryWalletFile wallet_file{"bbc-stage5-1-wallet.dat"};
    bbc::storage::ChainStoreResult store =
        bbc::storage::ChainStore::initialize(directory.path());
    REQUIRE(store.has_value());
    REQUIRE(store.value().append(known_mined_block_one()).has_value());
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();
    REQUIRE(
        wallet.save(wallet_file.path(), "secret") ==
        bbc::wallet::WalletFileError::none
    );
    const std::string data_path = directory.path().string();
    const std::string wallet_path = wallet_file.path().string();
    const std::array<std::string_view, 6> arguments{
        "wallet", "balance", "--file", wallet_path, "--data-dir", data_path
    };
    std::ostringstream output;
    std::ostringstream error_output;
    bbc::app::PasswordReader password_reader = [](
        const std::string_view,
        std::string& password
    ) {
        password = "secret";
        return true;
    };

    CHECK(bbc::app::run(arguments, output, error_output, password_reader) == 0);
    CHECK(output.str().find("Balance (base units): 0") != std::string::npos);
    CHECK(output.str().find("Chain height: 1") != std::string::npos);
    CHECK(error_output.str().empty());
}
