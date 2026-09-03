#pragma once

#include "bbc/crypto/types.hpp"

#include <optional>

namespace bbc::wallet {
class Wallet;
}

namespace bbc::crypto {

class KeyPair final {
public:
    static KeyPair generate();
    static std::optional<KeyPair> from_private_key(ByteView private_key);

    KeyPair(const KeyPair&) = delete;
    KeyPair& operator=(const KeyPair&) = delete;
    KeyPair(KeyPair&& other) noexcept;
    KeyPair& operator=(KeyPair&& other) noexcept;
    ~KeyPair();

    [[nodiscard]] const PublicKey& public_key() const noexcept;
    [[nodiscard]] Signature sign(ByteView message) const;

private:
    friend class bbc::wallet::Wallet;

    KeyPair(PublicKey public_key, std::array<Byte, private_key_size>&& private_key);

    [[nodiscard]] ByteView private_key() const noexcept;

    PublicKey public_key_{};
    std::array<Byte, private_key_size> private_key_{};
};

[[nodiscard]] bool verify_signature(
    ByteView message,
    const Signature& signature,
    const PublicKey& public_key
);

}  // namespace bbc::crypto
