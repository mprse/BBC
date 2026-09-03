#include "bbc/crypto/keys.hpp"

#include "crypto/sodium_runtime.hpp"

#include <sodium.h>

#include <algorithm>
#include <utility>

namespace bbc::crypto {

static_assert(public_key_size == crypto_sign_PUBLICKEYBYTES);
static_assert(private_key_size == crypto_sign_SECRETKEYBYTES);
static_assert(signature_size == crypto_sign_BYTES);

KeyPair KeyPair::generate() {
    detail::ensure_sodium_initialized();

    PublicKey public_key{};
    std::array<Byte, private_key_size> private_key{};
    crypto_sign_keypair(public_key.data(), private_key.data());
    return KeyPair{public_key, std::move(private_key)};
}

std::optional<KeyPair> KeyPair::from_private_key(const ByteView private_key) {
    if (private_key.size() != private_key_size) {
        return std::nullopt;
    }

    detail::ensure_sodium_initialized();

    std::array<Byte, private_key_size> private_key_copy{};
    std::ranges::copy(private_key, private_key_copy.begin());

    PublicKey public_key{};
    std::array<Byte, private_key_size> canonical_private_key{};
    if (crypto_sign_seed_keypair(
            public_key.data(),
            canonical_private_key.data(),
            private_key_copy.data()
        ) != 0 ||
        sodium_memcmp(
            canonical_private_key.data(),
            private_key_copy.data(),
            private_key_copy.size()
        ) != 0) {
        sodium_memzero(canonical_private_key.data(), canonical_private_key.size());
        sodium_memzero(private_key_copy.data(), private_key_copy.size());
        return std::nullopt;
    }
    sodium_memzero(canonical_private_key.data(), canonical_private_key.size());

    return KeyPair{public_key, std::move(private_key_copy)};
}

KeyPair::KeyPair(
    PublicKey public_key,
    std::array<Byte, private_key_size>&& private_key
)
    : public_key_(public_key), private_key_(private_key) {
    sodium_memzero(private_key.data(), private_key.size());
}

KeyPair::KeyPair(KeyPair&& other) noexcept
    : public_key_(other.public_key_), private_key_(other.private_key_) {
    sodium_memzero(other.private_key_.data(), other.private_key_.size());
}

KeyPair& KeyPair::operator=(KeyPair&& other) noexcept {
    if (this != &other) {
        sodium_memzero(private_key_.data(), private_key_.size());
        public_key_ = other.public_key_;
        private_key_ = other.private_key_;
        sodium_memzero(other.private_key_.data(), other.private_key_.size());
    }
    return *this;
}

KeyPair::~KeyPair() {
    sodium_memzero(private_key_.data(), private_key_.size());
}

const PublicKey& KeyPair::public_key() const noexcept {
    return public_key_;
}

Signature KeyPair::sign(const ByteView message) const {
    detail::ensure_sodium_initialized();

    Signature signature{};
    crypto_sign_detached(
        signature.data(),
        nullptr,
        message.data(),
        static_cast<unsigned long long>(message.size()),
        private_key_.data()
    );
    return signature;
}

ByteView KeyPair::private_key() const noexcept {
    return private_key_;
}

bool verify_signature(
    const ByteView message,
    const Signature& signature,
    const PublicKey& public_key
) {
    detail::ensure_sodium_initialized();

    return crypto_sign_verify_detached(
               signature.data(),
               message.data(),
               static_cast<unsigned long long>(message.size()),
               public_key.data()
           ) == 0;
}

}  // namespace bbc::crypto
