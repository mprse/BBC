#pragma once

#include "bbc/crypto/keys.hpp"
#include "bbc/wallet/address.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <variant>

namespace bbc::wallet {

enum class WalletFileError {
    none,
    empty_password,
    file_already_exists,
    io_error,
    invalid_format,
    unsupported_version,
    key_derivation_failed,
    authentication_failed,
};

class Wallet final {
public:
    static Wallet create();
    static std::optional<Wallet> restore_from_private_key(crypto::ByteView private_key);

    Wallet(const Wallet&) = delete;
    Wallet& operator=(const Wallet&) = delete;
    Wallet(Wallet&&) noexcept = default;
    Wallet& operator=(Wallet&&) noexcept = default;
    ~Wallet() = default;

    [[nodiscard]] const crypto::PublicKey& public_key() const noexcept;
    [[nodiscard]] Address address() const;
    [[nodiscard]] crypto::Signature sign(crypto::ByteView message) const;

    [[nodiscard]] WalletFileError save(
        const std::filesystem::path& path,
        std::string_view password
    ) const;

private:
    explicit Wallet(crypto::KeyPair key_pair);

    crypto::KeyPair key_pair_;
};

using WalletLoadResult = std::variant<Wallet, WalletFileError>;

[[nodiscard]] WalletLoadResult load_wallet(
    const std::filesystem::path& path,
    std::string_view password
);

[[nodiscard]] std::string_view wallet_file_error_message(WalletFileError error) noexcept;

}  // namespace bbc::wallet
