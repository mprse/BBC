#pragma once

#include "bbc/crypto/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace bbc::wallet {

class Address final {
public:
    static Address from_public_key(const crypto::PublicKey& public_key);
    static Address from_hash(crypto::Hash256 hash);
    static std::optional<Address> parse(std::string_view encoded);

    [[nodiscard]] std::string_view value() const noexcept;
    [[nodiscard]] const crypto::Hash256& hash() const noexcept;

    bool operator==(const Address&) const = default;

private:
    explicit Address(crypto::Hash256 hash);

    crypto::Hash256 hash_{};
    std::string value_;
};

}  // namespace bbc::wallet
