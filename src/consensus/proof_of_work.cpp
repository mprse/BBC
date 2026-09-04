#include "bbc/consensus/proof_of_work.hpp"

#include "bbc/crypto/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace bbc::consensus {
namespace {

constexpr crypto::Hash256 make_fixed_target() {
    crypto::Hash256 target{};
    target.fill(0xFFU);
    target[0] = 0;
    target[1] = 0;
    target[2] = 0;
    return target;
}

constexpr crypto::Hash256 network_target = make_fixed_target();

void write_mining_nonce(
    crypto::Bytes& header,
    const std::uint64_t mining_nonce
) noexcept {
    for (unsigned int index = 0; index < sizeof(std::uint64_t); ++index) {
        header[chain::block_mining_nonce_offset + index] =
            static_cast<crypto::Byte>((mining_nonce >> (index * 8U)) & 0xFFU);
    }
}

}  // namespace

MiningResult::MiningResult(chain::Block block, const std::uint64_t attempts)
    : value_(std::move(block)), attempts_(attempts) {}

MiningResult::MiningResult(
    const ProofOfWorkError error,
    const std::uint64_t attempts,
    const std::optional<std::uint64_t> next_nonce
)
    : value_(error), attempts_(attempts), next_nonce_(next_nonce) {}

bool MiningResult::has_value() const noexcept {
    return std::holds_alternative<chain::Block>(value_);
}

chain::Block& MiningResult::value() & {
    return std::get<chain::Block>(value_);
}

const chain::Block& MiningResult::value() const& {
    return std::get<chain::Block>(value_);
}

chain::Block&& MiningResult::value() && {
    return std::get<chain::Block>(std::move(value_));
}

ProofOfWorkError MiningResult::error() const noexcept {
    const auto* error = std::get_if<ProofOfWorkError>(&value_);
    return error == nullptr ? ProofOfWorkError::none : *error;
}

std::uint64_t MiningResult::attempts() const noexcept {
    return attempts_;
}

std::optional<std::uint64_t> MiningResult::next_nonce() const noexcept {
    return next_nonce_;
}

const crypto::Hash256& fixed_difficulty_target() noexcept {
    return network_target;
}

bool hash_meets_target(
    const crypto::Hash256& hash,
    const crypto::Hash256& target
) noexcept {
    return std::lexicographical_compare(
        hash.begin(),
        hash.end(),
        target.begin(),
        target.end()
    );
}

ProofOfWorkError validate_proof_of_work(const chain::Block& block) {
    if (block.is_genesis()) {
        return ProofOfWorkError::none;
    }
    if (block.difficulty_target() != network_target) {
        return ProofOfWorkError::unexpected_target;
    }
    if (!hash_meets_target(block.id(), network_target)) {
        return ProofOfWorkError::target_not_met;
    }
    return ProofOfWorkError::none;
}

MiningResult mine_block(
    const chain::Block& candidate,
    const std::uint64_t maximum_attempts
) {
    if (candidate.is_genesis()) {
        return MiningResult{ProofOfWorkError::genesis_not_mineable, 0};
    }
    if (candidate.difficulty_target() != network_target) {
        return MiningResult{ProofOfWorkError::unexpected_target, 0};
    }

    crypto::Bytes header = candidate.serialize_header();
    std::uint64_t nonce = candidate.mining_nonce();
    std::uint64_t attempts = 0;
    while (attempts < maximum_attempts) {
        write_mining_nonce(header, nonce);
        ++attempts;
        if (hash_meets_target(crypto::sha256(header), network_target)) {
            return MiningResult{candidate.with_mining_nonce(nonce), attempts};
        }
        if (nonce == std::numeric_limits<std::uint64_t>::max()) {
            return MiningResult{ProofOfWorkError::nonce_exhausted, attempts};
        }
        ++nonce;
    }
    return MiningResult{
        ProofOfWorkError::attempt_limit_reached,
        attempts,
        nonce,
    };
}

std::string_view proof_of_work_error_message(
    const ProofOfWorkError error
) noexcept {
    switch (error) {
        case ProofOfWorkError::none:
            return "no error";
        case ProofOfWorkError::genesis_not_mineable:
            return "genesis block is not mined";
        case ProofOfWorkError::unexpected_target:
            return "block difficulty target does not match the network target";
        case ProofOfWorkError::target_not_met:
            return "block hash does not meet the difficulty target";
        case ProofOfWorkError::attempt_limit_reached:
            return "mining attempt limit was reached";
        case ProofOfWorkError::nonce_exhausted:
            return "mining nonce range was exhausted";
    }
    return "unknown Proof of Work error";
}

}  // namespace bbc::consensus
