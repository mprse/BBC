#include "bbc/crypto/keys.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

class TemporaryWalletFile final {
public:
    explicit TemporaryWalletFile(const std::string_view name)
        : path_(std::filesystem::temp_directory_path() /
                ("bbc-stage1-" + std::string{name} + ".wallet")) {
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

bbc::crypto::Bytes bytes(const std::string_view text) {
    return {text.begin(), text.end()};
}

}  // namespace

TEST_CASE("encrypted wallet round trip preserves its identity", "[wallet]") {
    TemporaryWalletFile file{"round-trip"};
    bbc::wallet::Wallet original = bbc::wallet::Wallet::create();
    const bbc::wallet::Address original_address = original.address();

    REQUIRE(original.save(file.path(), "correct horse battery staple") ==
            bbc::wallet::WalletFileError::none);

    bbc::wallet::WalletLoadResult load_result =
        bbc::wallet::load_wallet(file.path(), "correct horse battery staple");
    REQUIRE(std::holds_alternative<bbc::wallet::Wallet>(load_result));

    bbc::wallet::Wallet restored =
        std::move(std::get<bbc::wallet::Wallet>(load_result));
    CHECK(restored.address() == original_address);
    CHECK(restored.public_key() == original.public_key());

    const bbc::crypto::Bytes message = bytes("wallet round trip");
    const bbc::crypto::Signature signature = restored.sign(message);
    CHECK(bbc::crypto::verify_signature(message, signature, original.public_key()));
}

TEST_CASE("wallet refuses empty passwords and existing files", "[wallet]") {
    TemporaryWalletFile file{"save-errors"};
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();

    CHECK(wallet.save(file.path(), "") == bbc::wallet::WalletFileError::empty_password);
    REQUIRE(wallet.save(file.path(), "password") == bbc::wallet::WalletFileError::none);
    CHECK(wallet.save(file.path(), "password") ==
          bbc::wallet::WalletFileError::file_already_exists);
}

TEST_CASE("wrong wallet password is rejected", "[wallet]") {
    TemporaryWalletFile file{"wrong-password"};
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();
    REQUIRE(wallet.save(file.path(), "right password") ==
            bbc::wallet::WalletFileError::none);

    const bbc::wallet::WalletLoadResult result =
        bbc::wallet::load_wallet(file.path(), "wrong password");

    REQUIRE(std::holds_alternative<bbc::wallet::WalletFileError>(result));
    CHECK(std::get<bbc::wallet::WalletFileError>(result) ==
          bbc::wallet::WalletFileError::authentication_failed);
}

TEST_CASE("modified wallet ciphertext is rejected", "[wallet]") {
    TemporaryWalletFile file{"modified"};
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();
    REQUIRE(wallet.save(file.path(), "password") == bbc::wallet::WalletFileError::none);

    std::fstream wallet_file(file.path(), std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(wallet_file.seekg(-1, std::ios::end));
    char final_byte = 0;
    REQUIRE(wallet_file.get(final_byte));
    final_byte ^= 0x01;
    REQUIRE(wallet_file.seekp(-1, std::ios::end));
    REQUIRE(wallet_file.put(final_byte));
    wallet_file.close();

    const bbc::wallet::WalletLoadResult result =
        bbc::wallet::load_wallet(file.path(), "password");

    REQUIRE(std::holds_alternative<bbc::wallet::WalletFileError>(result));
    CHECK(std::get<bbc::wallet::WalletFileError>(result) ==
          bbc::wallet::WalletFileError::authentication_failed);
}

TEST_CASE("modified authenticated wallet metadata is rejected", "[wallet]") {
    TemporaryWalletFile file{"modified-metadata"};
    const bbc::wallet::Wallet wallet = bbc::wallet::Wallet::create();
    REQUIRE(wallet.save(file.path(), "password") == bbc::wallet::WalletFileError::none);

    // The public key begins at byte 64 and is authenticated as associated data.
    std::fstream wallet_file(file.path(), std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(wallet_file.seekg(64));
    char public_key_byte = 0;
    REQUIRE(wallet_file.get(public_key_byte));
    public_key_byte ^= 0x01;
    REQUIRE(wallet_file.seekp(64));
    REQUIRE(wallet_file.put(public_key_byte));
    wallet_file.close();

    const bbc::wallet::WalletLoadResult result =
        bbc::wallet::load_wallet(file.path(), "password");

    REQUIRE(std::holds_alternative<bbc::wallet::WalletFileError>(result));
    CHECK(std::get<bbc::wallet::WalletFileError>(result) ==
          bbc::wallet::WalletFileError::authentication_failed);
}

TEST_CASE("invalid wallet file is rejected before key derivation", "[wallet]") {
    TemporaryWalletFile file{"invalid"};
    {
        std::ofstream invalid_file(file.path(), std::ios::binary);
        invalid_file << "not a BBC wallet";
    }

    const bbc::wallet::WalletLoadResult result =
        bbc::wallet::load_wallet(file.path(), "password");

    REQUIRE(std::holds_alternative<bbc::wallet::WalletFileError>(result));
    CHECK(std::get<bbc::wallet::WalletFileError>(result) ==
          bbc::wallet::WalletFileError::invalid_format);
}
