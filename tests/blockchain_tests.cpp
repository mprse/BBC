#include "bbc/app/application.hpp"
#include "bbc/chain/blockchain.hpp"
#include "bbc/chain/state.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/crypto/hash.hpp"
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

bbc::chain::Block known_mined_block_one() {
    bbc::chain::BlockResult result = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        address_with_first_byte(0x44),
        1'788'393'600,
        bbc::consensus::fixed_difficulty_target(),
        7'707'263,
        {},
    });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

bbc::chain::Block state_only_block(
    const std::uint64_t height,
    const bbc::wallet::Address& reward_recipient,
    std::vector<bbc::transaction::SignedTransaction> transactions = {}
) {
    bbc::crypto::Hash256 parent{};
    parent.front() = 1;
    bbc::chain::BlockResult result = bbc::chain::create_block({
        height,
        parent,
        reward_recipient,
        height,
        bbc::consensus::fixed_difficulty_target(),
        0,
        std::move(transactions),
    });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

class TemporaryBlockFile final {
public:
    TemporaryBlockFile()
        : path_(std::filesystem::temp_directory_path() /
                "bbc-stage5-chain-test.bbcblock") {
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
    TemporaryWalletFile()
        : path_(std::filesystem::temp_directory_path() /
                "bbc-stage5-wallet-test.dat") {
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

TEST_CASE("a new blockchain contains only canonical Genesis", "[chain]") {
    const bbc::chain::Blockchain blockchain;

    CHECK(blockchain.block_count() == 1);
    CHECK(blockchain.tip().id() == bbc::chain::genesis_block().id());
    CHECK(blockchain.state().account_count() == 0);
}

TEST_CASE("appending a mined reward-only block creates miner funds", "[chain][state]") {
    bbc::chain::Blockchain blockchain;
    const bbc::chain::Block block = known_mined_block_one();

    const bbc::chain::AppendResult result = blockchain.append(block);

    REQUIRE(result.has_value());
    CHECK(blockchain.tip().id() == block.id());
    CHECK(blockchain.block_count() == 2);
    REQUIRE(block.reward_recipient().has_value());
    CHECK(
        blockchain.state().account(*block.reward_recipient()).balance ==
        bbc::chain::block_subsidy
    );
    CHECK(blockchain.state().account(*block.reward_recipient()).next_nonce == 0);
}

TEST_CASE("mined blocks apply a signed transfer end to end", "[chain][state]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    const bbc::wallet::Address recipient = address_with_first_byte(0x22);
    const bbc::wallet::Address second_miner = address_with_first_byte(0x33);
    bbc::chain::Blockchain blockchain;

    bbc::chain::BlockResult first = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        sender.address(),
        1,
        bbc::consensus::fixed_difficulty_target(),
        38'993'250,
        {},
    });
    REQUIRE(first.has_value());
    CHECK(
        bbc::crypto::to_upper_hex(first.value().id()) ==
        "0000008A44718860CDEAF9703375E4B73CED3D23117B3C326C9CD58C7253EA20"
    );
    REQUIRE(blockchain.append(std::move(first).value()).has_value());

    bbc::transaction::TransactionResult transaction =
        bbc::transaction::sign_transaction(
            sender,
            {recipient, 1'000'000'000, 25, 0}
        );
    REQUIRE(transaction.has_value());
    std::vector<bbc::transaction::SignedTransaction> transactions;
    transactions.push_back(std::move(transaction).value());
    bbc::chain::BlockResult second = bbc::chain::create_block({
        2,
        blockchain.tip().id(),
        second_miner,
        2,
        bbc::consensus::fixed_difficulty_target(),
        22'538'913,
        std::move(transactions),
    });
    REQUIRE(second.has_value());
    CHECK(
        bbc::crypto::to_upper_hex(second.value().id()) ==
        "000000B2742E044AECE2E0483A46F17DEA4C4D71E87368D599E9EC40C2DD2876"
    );

    REQUIRE(blockchain.append(std::move(second).value()).has_value());
    CHECK(blockchain.tip().height() == 2);
    CHECK(
        blockchain.state().account(sender.address()) ==
        bbc::chain::AccountState{
            bbc::chain::block_subsidy - 1'000'000'025,
            1,
        }
    );
    CHECK(blockchain.state().account(recipient).balance == 1'000'000'000);
    CHECK(
        blockchain.state().account(second_miner).balance ==
        bbc::chain::block_subsidy + 25
    );
}

TEST_CASE("chain linkage timestamp and Proof of Work failures are atomic", "[chain]") {
    const bbc::chain::Block valid = known_mined_block_one();

    bbc::chain::Blockchain wrong_height_chain;
    bbc::chain::BlockResult wrong_height = bbc::chain::create_block({
        2,
        bbc::chain::genesis_block().id(),
        address_with_first_byte(0x44),
        valid.timestamp(),
        bbc::consensus::fixed_difficulty_target(),
        valid.mining_nonce(),
        {},
    });
    REQUIRE(wrong_height.has_value());
    CHECK(
        wrong_height_chain.append(std::move(wrong_height).value()).error ==
        bbc::chain::ChainError::unexpected_height
    );
    CHECK(wrong_height_chain.block_count() == 1);

    bbc::chain::Blockchain stale_time_chain;
    bbc::chain::BlockResult stale_time = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        address_with_first_byte(0x44),
        0,
        bbc::consensus::fixed_difficulty_target(),
        valid.mining_nonce(),
        {},
    });
    REQUIRE(stale_time.has_value());
    CHECK(
        stale_time_chain.append(std::move(stale_time).value()).error ==
        bbc::chain::ChainError::timestamp_not_increasing
    );
    CHECK(stale_time_chain.state().account_count() == 0);

    bbc::chain::Blockchain invalid_work_chain;
    CHECK(
        invalid_work_chain.append(valid.with_mining_nonce(0)).error ==
        bbc::chain::ChainError::invalid_proof_of_work
    );
    CHECK(invalid_work_chain.block_count() == 1);
}

TEST_CASE("state applies ordered transfers fees nonces and miner rewards", "[state]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    const bbc::wallet::Address recipient = address_with_first_byte(0x22);
    const bbc::wallet::Address second_miner = address_with_first_byte(0x33);
    bbc::chain::ChainState state;

    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(1, sender.address())
        ).has_value()
    );

    bbc::transaction::TransactionResult signed_transaction =
        bbc::transaction::sign_transaction(
            sender,
            {recipient, 1'000'000'000, 25, 0}
        );
    REQUIRE(signed_transaction.has_value());
    std::vector<bbc::transaction::SignedTransaction> transactions;
    transactions.push_back(std::move(signed_transaction).value());

    const bbc::chain::StateTransitionResult result =
        bbc::chain::apply_block_state(
            state,
            state_only_block(2, second_miner, std::move(transactions))
        );

    REQUIRE(result.has_value());
    CHECK(
        state.account(sender.address()) ==
        bbc::chain::AccountState{
            bbc::chain::block_subsidy - 1'000'000'025,
            1,
        }
    );
    CHECK(state.account(recipient).balance == 1'000'000'000);
    CHECK(
        state.account(second_miner).balance ==
        bbc::chain::block_subsidy + 25
    );
}

TEST_CASE("invalid transaction state does not partially change balances", "[state]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    const bbc::wallet::Address recipient = address_with_first_byte(0x22);
    bbc::chain::ChainState state;
    REQUIRE(
        bbc::chain::apply_block_state(
            state,
            state_only_block(1, sender.address())
        ).has_value()
    );
    const bbc::chain::AccountState original = state.account(sender.address());

    bbc::transaction::TransactionResult wrong_nonce =
        bbc::transaction::sign_transaction(sender, {recipient, 10, 0, 1});
    REQUIRE(wrong_nonce.has_value());
    std::vector<bbc::transaction::SignedTransaction> transactions;
    transactions.push_back(std::move(wrong_nonce).value());

    const bbc::chain::StateTransitionResult result =
        bbc::chain::apply_block_state(
            state,
            state_only_block(2, address_with_first_byte(0x33), std::move(transactions))
        );

    CHECK_FALSE(result.has_value());
    CHECK(result.error == bbc::chain::StateTransitionError::invalid_account_nonce);
    CHECK(result.transaction_index == 0);
    CHECK(state.account(sender.address()) == original);
    CHECK(state.account(recipient).balance == 0);
}

TEST_CASE("state rejects spending funds that do not exist", "[state]") {
    const bbc::wallet::Wallet sender = deterministic_wallet();
    const bbc::wallet::Address recipient = address_with_first_byte(0x22);
    bbc::transaction::TransactionResult transaction =
        bbc::transaction::sign_transaction(sender, {recipient, 1, 0, 0});
    REQUIRE(transaction.has_value());
    std::vector<bbc::transaction::SignedTransaction> transactions;
    transactions.push_back(std::move(transaction).value());
    bbc::chain::ChainState state;

    const bbc::chain::StateTransitionResult result =
        bbc::chain::apply_block_state(
            state,
            state_only_block(1, address_with_first_byte(0x33), std::move(transactions))
        );

    CHECK_FALSE(result.has_value());
    CHECK(result.error == bbc::chain::StateTransitionError::insufficient_balance);
    CHECK(result.transaction_index == 0);
    CHECK(state.account_count() == 0);
}

TEST_CASE("chain CLI replays blocks and reports a balance", "[application][chain]") {
    TemporaryBlockFile block_file;
    const bbc::chain::Block block = known_mined_block_one();
    REQUIRE(
        bbc::chain::save_block(block, block_file.path()) ==
        bbc::chain::BlockError::none
    );
    REQUIRE(block.reward_recipient().has_value());
    const std::string path = block_file.path().string();
    const std::string address{block.reward_recipient()->value()};
    const std::array<std::string_view, 6> arguments{
        "chain", "balance", "--address", address, "--block", path
    };
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(bbc::app::run(arguments, output, error_output) == 0);
    CHECK(
        output.str().find("Balance (base units): 5000000000") !=
        std::string::npos
    );
    CHECK(output.str().find("Next account nonce: 0") != std::string::npos);
    CHECK(output.str().find("Chain height: 1") != std::string::npos);
    CHECK(error_output.str().empty());
}

TEST_CASE("wallet balance uses an explicit wallet and verified blocks", "[application][chain]") {
    TemporaryBlockFile block_file;
    TemporaryWalletFile wallet_file;
    REQUIRE(
        bbc::chain::save_block(known_mined_block_one(), block_file.path()) ==
        bbc::chain::BlockError::none
    );
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();
    REQUIRE(
        wallet.save(wallet_file.path(), "secret") ==
        bbc::wallet::WalletFileError::none
    );
    const std::string block_path = block_file.path().string();
    const std::string wallet_path = wallet_file.path().string();
    const std::array<std::string_view, 6> arguments{
        "wallet", "balance", "--file", wallet_path, "--block", block_path
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

    CHECK(
        bbc::app::run(arguments, output, error_output, password_reader) == 0
    );
    CHECK(output.str().find("Balance (base units): 0") != std::string::npos);
    CHECK(output.str().find("Chain height: 1") != std::string::npos);
    CHECK(error_output.str().empty());
}
