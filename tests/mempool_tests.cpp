#include "bbc/app/application.hpp"
#include "bbc/chain/block.hpp"
#include "bbc/chain/state.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/mempool/mempool.hpp"
#include "bbc/storage/chain_store.hpp"
#include "bbc/storage/mempool_store.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bbc::wallet::Address address_with_first_byte(const bbc::crypto::Byte byte) {
    bbc::crypto::Hash256 hash{};
    hash.front() = byte;
    return bbc::wallet::Address::from_hash(hash);
}

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

bbc::chain::Block state_only_block(
    const std::uint64_t height,
    const bbc::wallet::Address& reward_recipient,
    std::vector<bbc::transaction::SignedTransaction> transactions = {}
) {
    bbc::crypto::Hash256 parent{};
    parent.front() = 1;
    bbc::chain::BlockResult block = bbc::chain::create_block({
        height,
        parent,
        reward_recipient,
        height,
        bbc::consensus::fixed_difficulty_target(),
        0,
        std::move(transactions),
    });
    REQUIRE(block.has_value());
    return std::move(block).value();
}

bbc::chain::Block known_sender_reward_block() {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    bbc::chain::BlockResult block = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        sender.address(),
        1,
        bbc::consensus::fixed_difficulty_target(),
        38'993'250,
        {},
    });
    REQUIRE(block.has_value());
    return std::move(block).value();
}

bbc::transaction::SignedTransaction signed_transaction(
    const bbc::wallet::Wallet& sender,
    const std::uint64_t amount,
    const std::uint64_t fee,
    const std::uint64_t nonce,
    const bbc::crypto::Byte recipient_byte = 0x22
) {
    bbc::transaction::TransactionResult result =
        bbc::transaction::sign_transaction(
            sender,
            {address_with_first_byte(recipient_byte), amount, fee, nonce}
        );
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

class TemporaryFile final {
public:
    explicit TemporaryFile(const std::string_view name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    ~TemporaryFile() {
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

TEST_CASE("mempool enforces nonce sequence duplicates and reserved balance", "[mempool]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    bbc::chain::ChainState state;
    REQUIRE(
        bbc::chain::apply_block_state(state, state_only_block(1, sender.address()))
            .has_value()
    );
    bbc::mempool::Mempool pool{state};

    const bbc::transaction::SignedTransaction first =
        signed_transaction(sender, 4'000'000'000, 10, 0);
    CHECK(pool.add(first) == bbc::mempool::MempoolError::none);
    CHECK(
        pool.add(first) ==
        bbc::mempool::MempoolError::duplicate_transaction
    );
    CHECK(
        pool.add(signed_transaction(sender, 1, 20, 0)) ==
        bbc::mempool::MempoolError::account_nonce_conflict
    );
    CHECK(
        pool.add(signed_transaction(sender, 1, 20, 2)) ==
        bbc::mempool::MempoolError::unexpected_account_nonce
    );
    CHECK(
        pool.add(signed_transaction(sender, 1'000'000'000, 0, 1)) ==
        bbc::mempool::MempoolError::insufficient_balance
    );
    CHECK(
        pool.add(signed_transaction(sender, 999'999'980, 10, 1)) ==
        bbc::mempool::MempoolError::none
    );
    CHECK(pool.size() == 2);
}

TEST_CASE("mempool fee selection preserves each sender nonce order", "[mempool]") {
    const bbc::wallet::Wallet first_sender = deterministic_wallet();
    const bbc::wallet::Wallet second_sender = bbc::wallet::Wallet::create();
    bbc::chain::ChainState state;
    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(1, first_sender.address())
        ).has_value()
    );
    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(2, second_sender.address())
        ).has_value()
    );
    bbc::mempool::Mempool pool{state};
    const bbc::transaction::SignedTransaction first_nonce =
        signed_transaction(first_sender, 1, 1, 0);
    const bbc::transaction::SignedTransaction second_nonce =
        signed_transaction(first_sender, 1, 100, 1);
    const bbc::transaction::SignedTransaction other_sender =
        signed_transaction(second_sender, 1, 50, 0, 0x33);
    REQUIRE(pool.add(first_nonce) == bbc::mempool::MempoolError::none);
    REQUIRE(pool.add(second_nonce) == bbc::mempool::MempoolError::none);
    REQUIRE(pool.add(other_sender) == bbc::mempool::MempoolError::none);

    const std::vector<bbc::transaction::SignedTransaction> selected = pool.select(3);

    REQUIRE(selected.size() == 3);
    CHECK(selected[0].id() == other_sender.id());
    CHECK(selected[1].id() == first_nonce.id());
    CHECK(selected[2].id() == second_nonce.id());
}

TEST_CASE("mempool revalidation removes confirmed transactions and retains the next nonce", "[mempool]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    bbc::chain::ChainState state;
    REQUIRE(
        bbc::chain::apply_block_state(state, state_only_block(1, sender.address()))
            .has_value()
    );
    const bbc::transaction::SignedTransaction first =
        signed_transaction(sender, 100, 1, 0);
    const bbc::transaction::SignedTransaction second =
        signed_transaction(sender, 200, 2, 1);
    bbc::mempool::Mempool pool{state};
    REQUIRE(pool.add(first) == bbc::mempool::MempoolError::none);
    REQUIRE(pool.add(second) == bbc::mempool::MempoolError::none);

    std::vector<bbc::transaction::SignedTransaction> confirmed{first};
    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(2, address_with_first_byte(0x44), std::move(confirmed))
        ).has_value()
    );

    CHECK(pool.revalidate(state) == 1);
    REQUIRE(pool.size() == 1);
    CHECK(pool.transactions().front().id() == second.id());
}

TEST_CASE("mempool store survives reopen and revalidates its cache", "[mempool][storage]") {
    TemporaryDataDirectory directory{"bbc-stage6-mempool-store"};
    const bbc::wallet::Wallet sender = deterministic_wallet();
    bbc::chain::ChainState state;
    REQUIRE(
        bbc::chain::apply_block_state(state, state_only_block(1, sender.address()))
            .has_value()
    );
    const bbc::transaction::SignedTransaction first =
        signed_transaction(sender, 100, 1, 0);
    const bbc::transaction::SignedTransaction second =
        signed_transaction(sender, 200, 2, 1);
    bbc::storage::MempoolStoreResult store =
        bbc::storage::MempoolStore::open(directory.path(), state);
    REQUIRE(store.has_value());
    REQUIRE(store.value().add(first).has_value());
    REQUIRE(store.value().add(second).has_value());
    CHECK(std::filesystem::is_regular_file(store.value().database_path()));

    bbc::storage::MempoolStoreResult reopened =
        bbc::storage::MempoolStore::open(directory.path(), state);
    REQUIRE(reopened.has_value());
    CHECK(reopened.value().mempool().size() == 2);

    std::vector<bbc::transaction::SignedTransaction> confirmed{first};
    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(2, address_with_first_byte(0x44), std::move(confirmed))
        ).has_value()
    );
    const bbc::storage::MempoolStoreRevalidationResult revalidated =
        reopened.value().revalidate(state);
    REQUIRE(revalidated.has_value());
    CHECK(revalidated.removed == 1);

    bbc::storage::MempoolStoreResult final_open =
        bbc::storage::MempoolStore::open(directory.path(), state);
    REQUIRE(final_open.has_value());
    REQUIRE(final_open.value().mempool().size() == 1);
    CHECK(final_open.value().mempool().transactions().front().id() == second.id());
}

TEST_CASE("mempool store restores eligible transactions after a reorganization", "[mempool][storage][fork]") {
    TemporaryDataDirectory directory{"bbc-stage8-1-mempool-reorg"};
    const bbc::wallet::Wallet sender = deterministic_wallet();
    const bbc::transaction::SignedTransaction transaction =
        signed_transaction(sender, 100, 5, 0);
    const bbc::transaction::SignedTransaction descendant =
        signed_transaction(sender, 200, 6, 1);

    bbc::chain::ChainState common_state;
    REQUIRE(
        bbc::chain::apply_block_state(
            common_state,
            state_only_block(1, sender.address())
        ).has_value()
    );
    bbc::chain::ChainState confirmed_state = common_state;
    const bbc::chain::Block detached_block = state_only_block(
        2,
        address_with_first_byte(0x44),
        {transaction}
    );
    REQUIRE(
        bbc::chain::apply_block_state(confirmed_state, detached_block).has_value()
    );

    bbc::storage::MempoolStoreResult store =
        bbc::storage::MempoolStore::open(directory.path(), confirmed_state);
    REQUIRE(store.has_value());
    REQUIRE(store.value().add(descendant).has_value());
    const bbc::storage::MempoolStoreReorganizationResult reconciled =
        store.value().reconcile_reorganization(common_state, {detached_block});

    REQUIRE(reconciled.has_value());
    CHECK(reconciled.removed == 0);
    CHECK(reconciled.detached == 1);
    CHECK(reconciled.restored == 1);
    REQUIRE(store.value().mempool().size() == 2);
    CHECK(store.value().mempool().transactions()[0].id() == transaction.id());
    CHECK(store.value().mempool().transactions()[1].id() == descendant.id());

    bbc::storage::MempoolStoreResult reopened =
        bbc::storage::MempoolStore::open(directory.path(), common_state);
    REQUIRE(reopened.has_value());
    REQUIRE(reopened.value().mempool().size() == 2);
    CHECK(reopened.value().mempool().transactions()[0].id() == transaction.id());
    CHECK(reopened.value().mempool().transactions()[1].id() == descendant.id());
}

TEST_CASE("mempool CLI creates a transaction-backed block candidate", "[application][mempool]") {
    TemporaryDataDirectory directory{"bbc-stage6-mempool-cli"};
    TemporaryFile transaction_file{"bbc-stage6-mempool-cli.bbctx"};
    TemporaryFile candidate_file{"bbc-stage6-candidate.bbcblock"};
    TemporaryFile mined_file{"bbc-stage6-mined.bbcblock"};
    const bbc::wallet::Wallet sender = deterministic_wallet();
    bbc::storage::ChainStoreResult chain_store =
        bbc::storage::ChainStore::initialize(directory.path());
    REQUIRE(chain_store.has_value());
    REQUIRE(chain_store.value().append(known_sender_reward_block()).has_value());
    const bbc::transaction::SignedTransaction transaction =
        signed_transaction(sender, 1'000'000'000, 25, 0);
    REQUIRE(
        bbc::transaction::save_transaction(transaction, transaction_file.path()) ==
        bbc::transaction::TransactionError::none
    );

    const std::string data_path = directory.path().string();
    const std::string transaction_path = transaction_file.path().string();
    const std::array<std::string_view, 6> add_arguments{
        "mempool", "add", "--data-dir", data_path,
        "--transaction", transaction_path,
    };
    std::ostringstream add_output;
    std::ostringstream error_output;
    REQUIRE(bbc::app::run(add_arguments, add_output, error_output) == 0);
    CHECK(add_output.str().find("Pending transactions: 1") != std::string::npos);

    const std::string reward{sender.address().value()};
    const std::string candidate_path = candidate_file.path().string();
    const std::array<std::string_view, 10> candidate_arguments{
        "block", "candidate", "--data-dir", data_path,
        "--reward-to", reward, "--timestamp", "2", "--out", candidate_path,
    };
    std::ostringstream candidate_output;
    REQUIRE(
        bbc::app::run(candidate_arguments, candidate_output, error_output) == 0
    );
    bbc::chain::BlockResult candidate =
        bbc::chain::load_block(candidate_file.path());
    REQUIRE(candidate.has_value());
    CHECK(candidate.value().height() == 2);
    CHECK(candidate.value().previous_block_hash() == known_sender_reward_block().id());
    REQUIRE(candidate.value().transactions().size() == 1);
    CHECK(candidate.value().transactions().front().id() == transaction.id());

    std::vector<bbc::transaction::SignedTransaction> transactions{transaction};
    bbc::chain::BlockResult mined = bbc::chain::create_block({
        2,
        known_sender_reward_block().id(),
        address_with_first_byte(0x33),
        2,
        bbc::consensus::fixed_difficulty_target(),
        22'538'913,
        std::move(transactions),
    });
    REQUIRE(mined.has_value());
    REQUIRE(
        bbc::chain::save_block(mined.value(), mined_file.path()) ==
        bbc::chain::BlockError::none
    );
    const std::string mined_path = mined_file.path().string();
    const std::array<std::string_view, 6> chain_add_arguments{
        "chain", "add", "--data-dir", data_path, "--block", mined_path,
    };
    std::ostringstream chain_output;
    REQUIRE(bbc::app::run(chain_add_arguments, chain_output, error_output) == 0);
    CHECK(
        chain_output.str().find("Pending transactions after revalidation: 0") !=
        std::string::npos
    );

    bbc::storage::ChainStoreResult final_chain =
        bbc::storage::ChainStore::open(directory.path());
    REQUIRE(final_chain.has_value());
    bbc::storage::MempoolStoreResult final_mempool =
        bbc::storage::MempoolStore::open(
            directory.path(),
            final_chain.value().blockchain().state()
        );
    REQUIRE(final_mempool.has_value());
    CHECK(final_mempool.value().mempool().size() == 0);
    CHECK(error_output.str().empty());
}
