#include "bbc/wallet/wallet.hpp"

#include "crypto/sodium_runtime.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>

namespace bbc::wallet {
namespace {

constexpr std::array<crypto::Byte, 4> wallet_magic{'B', 'B', 'C', 'W'};
constexpr crypto::Byte wallet_format_version = 1;
constexpr crypto::Byte argon2id_algorithm = 1;
constexpr crypto::Byte xchacha20_poly1305_algorithm = 1;
constexpr crypto::Byte reserved = 0;
constexpr std::uint64_t argon2id_operations_limit = 2;
constexpr std::uint64_t argon2id_memory_limit = 64ULL * 1024ULL * 1024ULL;

constexpr std::size_t salt_size = crypto_pwhash_SALTBYTES;
constexpr std::size_t nonce_size = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr std::size_t encryption_key_size = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr std::size_t authentication_tag_size = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr std::size_t metadata_size = 4;
constexpr std::size_t integer_fields_size = 2 * sizeof(std::uint64_t);
constexpr std::size_t authenticated_header_size =
    wallet_magic.size() + metadata_size + integer_fields_size + salt_size + nonce_size +
    crypto::public_key_size;
constexpr std::size_t encrypted_private_key_size =
    crypto::private_key_size + authentication_tag_size;
constexpr std::size_t wallet_file_size =
    authenticated_header_size + encrypted_private_key_size;

static_assert(crypto::public_key_size == crypto_sign_PUBLICKEYBYTES);
static_assert(crypto::private_key_size == crypto_sign_SECRETKEYBYTES);
static_assert(argon2id_operations_limit == crypto_pwhash_OPSLIMIT_INTERACTIVE);
static_assert(argon2id_memory_limit == crypto_pwhash_MEMLIMIT_INTERACTIVE);

template <std::size_t Size>
class SecureArray final {
public:
    SecureArray() = default;
    SecureArray(const SecureArray&) = delete;
    SecureArray& operator=(const SecureArray&) = delete;
    SecureArray(SecureArray&&) = delete;
    SecureArray& operator=(SecureArray&&) = delete;

    ~SecureArray() {
        sodium_memzero(bytes_.data(), bytes_.size());
    }

    [[nodiscard]] crypto::Byte* data() noexcept {
        return bytes_.data();
    }

    [[nodiscard]] const crypto::Byte* data() const noexcept {
        return bytes_.data();
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return bytes_.size();
    }

    [[nodiscard]] crypto::ByteView view() const noexcept {
        return bytes_;
    }

private:
    std::array<crypto::Byte, Size> bytes_{};
};

void append_u64_le(crypto::Bytes& output, const std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

std::uint64_t read_u64_le(const crypto::ByteView input, const std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < sizeof(std::uint64_t); ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

template <typename Range>
void append(crypto::Bytes& output, const Range& range) {
    output.insert(output.end(), range.begin(), range.end());
}

bool derive_encryption_key(
    SecureArray<encryption_key_size>& key,
    const std::string_view password,
    const crypto::Byte* salt,
    const std::uint64_t operations_limit,
    const std::uint64_t memory_limit
) {
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (memory_limit > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
    }

    return crypto_pwhash(
               key.data(),
               key.size(),
               password.data(),
               static_cast<unsigned long long>(password.size()),
               salt,
               static_cast<unsigned long long>(operations_limit),
               static_cast<std::size_t>(memory_limit),
               crypto_pwhash_ALG_ARGON2ID13
           ) == 0;
}

}  // namespace

WalletFileError Wallet::save(
    const std::filesystem::path& path,
    const std::string_view password
) const {
    if (password.empty()) {
        return WalletFileError::empty_password;
    }

    std::error_code file_error;
    if (std::filesystem::exists(path, file_error)) {
        return file_error ? WalletFileError::io_error : WalletFileError::file_already_exists;
    }
    if (file_error) {
        return WalletFileError::io_error;
    }

    crypto::detail::ensure_sodium_initialized();

    std::array<crypto::Byte, salt_size> salt{};
    std::array<crypto::Byte, nonce_size> nonce{};
    randombytes_buf(salt.data(), salt.size());
    randombytes_buf(nonce.data(), nonce.size());

    constexpr std::uint64_t operations_limit = argon2id_operations_limit;
    constexpr std::uint64_t memory_limit = argon2id_memory_limit;

    crypto::Bytes authenticated_header;
    authenticated_header.reserve(authenticated_header_size);
    append(authenticated_header, wallet_magic);
    authenticated_header.push_back(wallet_format_version);
    authenticated_header.push_back(argon2id_algorithm);
    authenticated_header.push_back(xchacha20_poly1305_algorithm);
    authenticated_header.push_back(reserved);
    append_u64_le(authenticated_header, operations_limit);
    append_u64_le(authenticated_header, memory_limit);
    append(authenticated_header, salt);
    append(authenticated_header, nonce);
    append(authenticated_header, public_key());

    SecureArray<encryption_key_size> encryption_key;
    if (!derive_encryption_key(
            encryption_key,
            password,
            salt.data(),
            operations_limit,
            memory_limit
        )) {
        return WalletFileError::key_derivation_failed;
    }

    crypto::Bytes ciphertext(encrypted_private_key_size);
    unsigned long long ciphertext_size = 0;
    const crypto::ByteView private_key = key_pair_.private_key();
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data(),
            &ciphertext_size,
            private_key.data(),
            static_cast<unsigned long long>(private_key.size()),
            authenticated_header.data(),
            static_cast<unsigned long long>(authenticated_header.size()),
            nullptr,
            nonce.data(),
            encryption_key.data()
        ) != 0 ||
        ciphertext_size != ciphertext.size()) {
        return WalletFileError::io_error;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return WalletFileError::io_error;
    }

    output.write(
        reinterpret_cast<const char*>(authenticated_header.data()),
        static_cast<std::streamsize>(authenticated_header.size())
    );
    output.write(
        reinterpret_cast<const char*>(ciphertext.data()),
        static_cast<std::streamsize>(ciphertext.size())
    );
    output.flush();
    const bool write_succeeded = output.good();
    output.close();

    if (!write_succeeded) {
        std::filesystem::remove(path, file_error);
        return WalletFileError::io_error;
    }

    return WalletFileError::none;
}

WalletLoadResult load_wallet(
    const std::filesystem::path& path,
    const std::string_view password
) {
    if (password.empty()) {
        return WalletFileError::empty_password;
    }

    std::error_code file_error;
    const std::uintmax_t size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        return WalletFileError::io_error;
    }
    if (size != wallet_file_size) {
        return WalletFileError::invalid_format;
    }

    crypto::Bytes encoded(wallet_file_size);
    std::ifstream input(path, std::ios::binary);
    if (!input.read(
            reinterpret_cast<char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size())
        )) {
        return WalletFileError::io_error;
    }

    const crypto::ByteView file{encoded};
    if (!std::ranges::equal(wallet_magic, file.first(wallet_magic.size()))) {
        return WalletFileError::invalid_format;
    }

    std::size_t offset = wallet_magic.size();
    const crypto::Byte version = file[offset++];
    if (version != wallet_format_version) {
        return WalletFileError::unsupported_version;
    }
    if (file[offset++] != argon2id_algorithm ||
        file[offset++] != xchacha20_poly1305_algorithm || file[offset++] != reserved) {
        return WalletFileError::invalid_format;
    }

    const std::uint64_t operations_limit = read_u64_le(file, offset);
    offset += sizeof(std::uint64_t);
    const std::uint64_t memory_limit = read_u64_le(file, offset);
    offset += sizeof(std::uint64_t);

    if (operations_limit != argon2id_operations_limit ||
        memory_limit != argon2id_memory_limit) {
        return WalletFileError::invalid_format;
    }

    const crypto::Byte* salt = file.data() + offset;
    offset += salt_size;
    const crypto::Byte* nonce = file.data() + offset;
    offset += nonce_size;

    crypto::PublicKey stored_public_key{};
    std::ranges::copy(
        file.subspan(offset, stored_public_key.size()),
        stored_public_key.begin()
    );
    offset += stored_public_key.size();

    if (offset != authenticated_header_size) {
        return WalletFileError::invalid_format;
    }

    crypto::detail::ensure_sodium_initialized();
    SecureArray<encryption_key_size> encryption_key;
    if (!derive_encryption_key(
            encryption_key,
            password,
            salt,
            operations_limit,
            memory_limit
        )) {
        return WalletFileError::key_derivation_failed;
    }

    SecureArray<crypto::private_key_size> private_key;
    unsigned long long private_key_size = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            private_key.data(),
            &private_key_size,
            nullptr,
            file.data() + authenticated_header_size,
            encrypted_private_key_size,
            file.data(),
            authenticated_header_size,
            nonce,
            encryption_key.data()
        ) != 0) {
        return WalletFileError::authentication_failed;
    }
    if (private_key_size != private_key.size()) {
        return WalletFileError::invalid_format;
    }

    std::optional<Wallet> wallet = Wallet::restore_from_private_key(private_key.view());
    if (!wallet.has_value() ||
        sodium_memcmp(
            wallet->public_key().data(),
            stored_public_key.data(),
            stored_public_key.size()
        ) != 0) {
        return WalletFileError::authentication_failed;
    }

    return std::move(*wallet);
}

std::string_view wallet_file_error_message(const WalletFileError error) noexcept {
    switch (error) {
        case WalletFileError::none:
            return "no error";
        case WalletFileError::empty_password:
            return "wallet password must not be empty";
        case WalletFileError::file_already_exists:
            return "wallet file already exists";
        case WalletFileError::io_error:
            return "wallet file I/O failed";
        case WalletFileError::invalid_format:
            return "wallet file format is invalid";
        case WalletFileError::unsupported_version:
            return "wallet file version is not supported";
        case WalletFileError::key_derivation_failed:
            return "wallet key derivation failed";
        case WalletFileError::authentication_failed:
            return "wallet password is incorrect or the file is corrupted";
    }
    return "unknown wallet error";
}

}  // namespace bbc::wallet
