#include "bbc/wallet/wallet.hpp"

#include <utility>

namespace bbc::wallet {

Wallet Wallet::create() {
    return Wallet{crypto::KeyPair::generate()};
}

std::optional<Wallet> Wallet::restore_from_private_key(const crypto::ByteView private_key) {
    std::optional<crypto::KeyPair> key_pair = crypto::KeyPair::from_private_key(private_key);
    if (!key_pair.has_value()) {
        return std::nullopt;
    }
    return Wallet{std::move(*key_pair)};
}

Wallet::Wallet(crypto::KeyPair key_pair) : key_pair_(std::move(key_pair)) {}

const crypto::PublicKey& Wallet::public_key() const noexcept {
    return key_pair_.public_key();
}

Address Wallet::address() const {
    return Address::from_public_key(public_key());
}

crypto::Signature Wallet::sign(const crypto::ByteView message) const {
    return key_pair_.sign(message);
}

}  // namespace bbc::wallet
