#include "bbc/chain/block.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/wallet/address.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>

namespace {

bbc::wallet::Address reward_address() {
    bbc::crypto::Hash256 hash{};
    hash.front() = 0x44;
    return bbc::wallet::Address::from_hash(hash);
}

bbc::chain::Block create_candidate(const std::uint64_t mining_nonce) {
    bbc::chain::BlockResult result = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        reward_address(),
        1'788'393'600,
        bbc::consensus::fixed_difficulty_target(),
        mining_nonce,
        {},
    });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

}  // namespace

TEST_CASE("fixed Proof of Work target is a protocol constant", "[pow]") {
    CHECK(
        bbc::crypto::to_upper_hex(bbc::consensus::fixed_difficulty_target()) ==
        "000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
    );
    CHECK(
        bbc::crypto::to_upper_hex(bbc::consensus::fixed_difficulty_target(2)) ==
        "000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
    );
    CHECK(bbc::chain::genesis_block(1).id() != bbc::chain::genesis_block(2).id());
}

TEST_CASE("Proof of Work comparison is strict and big endian", "[pow]") {
    bbc::crypto::Hash256 target{};
    target.back() = 2;
    bbc::crypto::Hash256 lower{};
    lower.back() = 1;
    bbc::crypto::Hash256 equal = target;
    bbc::crypto::Hash256 greater{};
    greater[30] = 1;

    CHECK(bbc::consensus::hash_meets_target(lower, target));
    CHECK_FALSE(bbc::consensus::hash_meets_target(equal, target));
    CHECK_FALSE(bbc::consensus::hash_meets_target(greater, target));
}

TEST_CASE("known mining nonce satisfies the fixed target", "[pow]") {
    constexpr std::uint64_t winning_nonce = 7'707'263;
    const bbc::chain::Block block = create_candidate(winning_nonce);

    CHECK(
        bbc::crypto::to_upper_hex(block.id()) ==
        "00000091566FACB46701A1D0633E0C89506B11C1FF4F150611BEBD9E6EC2FF58"
    );
    CHECK(
        bbc::consensus::validate_proof_of_work(block) ==
        bbc::consensus::ProofOfWorkError::none
    );
    CHECK(
        bbc::consensus::validate_proof_of_work(
            create_candidate(winning_nonce - 1)
        ) == bbc::consensus::ProofOfWorkError::target_not_met
    );
}

TEST_CASE("miner finds a valid nonce and reports attempts", "[pow]") {
    constexpr std::uint64_t winning_nonce = 7'707'263;
    bbc::consensus::MiningResult result =
        bbc::consensus::mine_block(create_candidate(winning_nonce), 1);

    REQUIRE(result.has_value());
    CHECK(result.attempts() == 1);
    CHECK(result.value().mining_nonce() == winning_nonce);
    CHECK(
        bbc::consensus::validate_proof_of_work(result.value()) ==
        bbc::consensus::ProofOfWorkError::none
    );
}

TEST_CASE("miner stops deterministically at configured boundaries", "[pow]") {
    bbc::consensus::MiningResult limited =
        bbc::consensus::mine_block(create_candidate(0), 1);
    CHECK_FALSE(limited.has_value());
    CHECK(limited.error() == bbc::consensus::ProofOfWorkError::attempt_limit_reached);
    CHECK(limited.attempts() == 1);
    REQUIRE(limited.next_nonce().has_value());
    CHECK(*limited.next_nonce() == 1);

    bbc::consensus::MiningResult exhausted = bbc::consensus::mine_block(
        create_candidate(std::numeric_limits<std::uint64_t>::max()),
        1
    );
    CHECK_FALSE(exhausted.has_value());
    CHECK(exhausted.error() == bbc::consensus::ProofOfWorkError::nonce_exhausted);
    CHECK(exhausted.attempts() == 1);
    CHECK_FALSE(exhausted.next_nonce().has_value());
}

TEST_CASE("Proof of Work rejects the wrong target and exempts Genesis", "[pow]") {
    bbc::crypto::Hash256 wrong_target{};
    wrong_target.fill(0xFFU);
    bbc::chain::BlockResult wrong_block = bbc::chain::create_block({
        1,
        bbc::chain::genesis_block().id(),
        reward_address(),
        1'788'393'600,
        wrong_target,
        0,
        {},
    });
    REQUIRE(wrong_block.has_value());
    CHECK(
        bbc::consensus::validate_proof_of_work(wrong_block.value()) ==
        bbc::consensus::ProofOfWorkError::unexpected_target
    );
    CHECK(
        bbc::consensus::validate_proof_of_work(bbc::chain::genesis_block()) ==
        bbc::consensus::ProofOfWorkError::none
    );
    CHECK(
        bbc::consensus::mine_block(bbc::chain::genesis_block(), 1).error() ==
        bbc::consensus::ProofOfWorkError::genesis_not_mineable
    );
}
