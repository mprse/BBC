#include "bbc/wallet/address.hpp"

#include "bbc/crypto/hash.hpp"

#include <utility>

namespace bbc::wallet {

Address Address::from_public_key(const crypto::PublicKey& public_key) {
    return Address{crypto::sha256(public_key)};
}

Address Address::from_hash(crypto::Hash256 hash) {
    return Address{std::move(hash)};
}

std::optional<Address> Address::parse(const std::string_view encoded) {
    constexpr std::string_view prefix = "BBC_";
    if (!encoded.starts_with(prefix)) {
        return std::nullopt;
    }

    crypto::Hash256 hash{};
    if (!crypto::decode_hex(encoded.substr(prefix.size()), hash)) {
        return std::nullopt;
    }
    return from_hash(std::move(hash));
}

Address::Address(crypto::Hash256 hash)
    : hash_(std::move(hash)), value_("BBC_" + crypto::to_upper_hex(hash_)) {}

std::string_view Address::value() const noexcept {
    return value_;
}

const crypto::Hash256& Address::hash() const noexcept {
    return hash_;
}

}  // namespace bbc::wallet
