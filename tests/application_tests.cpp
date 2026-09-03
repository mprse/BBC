#include "bbc/app/application.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class TemporaryCliWallet final {
public:
    TemporaryCliWallet()
        : path_(std::filesystem::temp_directory_path() / "bbc-cli-test.wallet") {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryCliWallet(const TemporaryCliWallet&) = delete;
    TemporaryCliWallet& operator=(const TemporaryCliWallet&) = delete;

    ~TemporaryCliWallet() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class TemporaryCliSettings final {
public:
    TemporaryCliSettings()
        : path_(std::filesystem::temp_directory_path() / "bbc-cli-test-settings") {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryCliSettings(const TemporaryCliSettings&) = delete;
    TemporaryCliSettings& operator=(const TemporaryCliSettings&) = delete;

    ~TemporaryCliSettings() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class TemporaryCliTransaction final {
public:
    TemporaryCliTransaction()
        : path_(std::filesystem::temp_directory_path() / "bbc-cli-test.bbctx") {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryCliTransaction(const TemporaryCliTransaction&) = delete;
    TemporaryCliTransaction& operator=(const TemporaryCliTransaction&) = delete;

    ~TemporaryCliTransaction() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bbc::app::PasswordReader passwords(std::vector<std::string> values) {
    return [values = std::move(values), index = std::size_t{0}](
               const std::string_view,
               std::string& password
           ) mutable {
        if (index >= values.size()) {
            return false;
        }
        password = values[index++];
        return true;
    };
}

std::string output_field(const std::string_view output, const std::string_view prefix) {
    const std::size_t start = output.find(prefix);
    if (start == std::string_view::npos) {
        return {};
    }
    const std::size_t value_start = start + prefix.size();
    const std::size_t end = output.find('\n', value_start);
    return std::string{output.substr(value_start, end - value_start)};
}

}  // namespace

TEST_CASE("help is the default command", "[application]") {
    std::ostringstream output;
    std::ostringstream error_output;

    const int result = bbc::app::run({}, output, error_output);

    CHECK(result == 0);
    CHECK(output.str().find("Bi-Bi-Coin (BBC)") != std::string::npos);
    CHECK(error_output.str().empty());
}

TEST_CASE("version command displays the project version", "[application]") {
    constexpr std::array<std::string_view, 1> arguments{"version"};
    std::ostringstream output;
    std::ostringstream error_output;

    const int result = bbc::app::run(arguments, output, error_output);

    CHECK(result == 0);
    CHECK(output.str() == "BBC 0.1.0\n");
    CHECK(error_output.str().empty());
}

TEST_CASE("unknown command reports a usage error", "[application]") {
    constexpr std::array<std::string_view, 1> arguments{"unknown"};
    std::ostringstream output;
    std::ostringstream error_output;

    const int result = bbc::app::run(arguments, output, error_output);

    CHECK(result == 2);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("Unknown command: unknown") != std::string::npos);
}

TEST_CASE("wallet demo creates an address and verifies a signature", "[application][wallet]") {
    constexpr std::array<std::string_view, 1> arguments{"wallet-demo"};
    std::ostringstream output;
    std::ostringstream error_output;

    const int result = bbc::app::run(arguments, output, error_output);

    CHECK(result == 0);
    CHECK(output.str().find("Wallet address: BBC_") != std::string::npos);
    CHECK(output.str().find("Ed25519 signature verification: success") !=
          std::string::npos);
    CHECK(error_output.str().empty());
}

TEST_CASE("wallet commands create and reopen an encrypted wallet", "[application][wallet]") {
    TemporaryCliWallet wallet_file;
    const std::string path = wallet_file.path().string();
    const std::array<std::string_view, 4> create_arguments{
        "wallet", "create", "--file", path
    };
    std::ostringstream create_output;
    std::ostringstream create_error;
    bbc::app::PasswordReader create_passwords = passwords({"secret", "secret"});

    REQUIRE(
        bbc::app::run(
            create_arguments,
            create_output,
            create_error,
            create_passwords
        ) == 0
    );
    REQUIRE(std::filesystem::is_regular_file(wallet_file.path()));
    REQUIRE(create_error.str().empty());
    const std::string created_address = output_field(create_output.str(), "Address: ");
    REQUIRE(created_address.starts_with("BBC_"));

    const std::array<std::string_view, 4> address_arguments{
        "wallet", "address", "--file", path
    };
    std::ostringstream address_output;
    std::ostringstream address_error;
    bbc::app::PasswordReader address_password = passwords({"secret"});

    CHECK(
        bbc::app::run(
            address_arguments,
            address_output,
            address_error,
            address_password
        ) == 0
    );
    CHECK(output_field(address_output.str(), "Address: ") == created_address);
    CHECK(address_error.str().empty());
}

TEST_CASE("wallet commands remember and use the selected wallet", "[application][wallet]") {
    TemporaryCliWallet wallet_file;
    TemporaryCliSettings settings;
    const std::string path = wallet_file.path().string();
    const std::array<std::string_view, 5> create_arguments{
        "wallet", "create", "--file", path, "--load"
    };
    std::ostringstream create_output;
    std::ostringstream error_output;
    bbc::app::PasswordReader create_passwords = passwords({"secret", "secret"});

    REQUIRE(
        bbc::app::run(
            create_arguments,
            create_output,
            error_output,
            create_passwords,
            settings.path()
        ) == 0
    );
    REQUIRE(create_output.str().find("Selected wallet: ") != std::string::npos);
    REQUIRE(error_output.str().empty());

    std::ifstream selection_file(settings.path() / "selected-wallet", std::ios::binary);
    const std::string selection_contents{
        std::istreambuf_iterator<char>{selection_file},
        std::istreambuf_iterator<char>{},
    };
    CHECK(selection_contents.find("secret") == std::string::npos);

    constexpr std::array<std::string_view, 2> selected_arguments{"wallet", "selected"};
    std::ostringstream selected_output;
    CHECK(
        bbc::app::run(
            selected_arguments,
            selected_output,
            error_output,
            {},
            settings.path()
        ) == 0
    );
    CHECK(selected_output.str().find(std::filesystem::absolute(wallet_file.path()).string()) !=
          std::string::npos);

    constexpr std::array<std::string_view, 2> address_arguments{"wallet", "address"};
    std::ostringstream address_output;
    bbc::app::PasswordReader address_password = passwords({"secret"});
    CHECK(
        bbc::app::run(
            address_arguments,
            address_output,
            error_output,
            address_password,
            settings.path()
        ) == 0
    );
    CHECK(output_field(address_output.str(), "Address: ") ==
          output_field(create_output.str(), "Address: "));

    const std::array<std::string_view, 4> sign_arguments{
        "wallet", "sign", "--message", "selected wallet message"
    };
    std::ostringstream sign_output;
    bbc::app::PasswordReader sign_password = passwords({"secret"});
    CHECK(
        bbc::app::run(
            sign_arguments,
            sign_output,
            error_output,
            sign_password,
            settings.path()
        ) == 0
    );
    CHECK(output_field(sign_output.str(), "Signature: ").size() == 128);
}

TEST_CASE("wallet select validates the password before remembering a wallet", "[application][wallet]") {
    TemporaryCliWallet wallet_file;
    TemporaryCliSettings settings;
    const std::string path = wallet_file.path().string();
    const std::array<std::string_view, 4> create_arguments{
        "wallet", "create", "--file", path
    };
    std::ostringstream output;
    std::ostringstream error_output;
    bbc::app::PasswordReader create_passwords = passwords({"secret", "secret"});
    REQUIRE(
        bbc::app::run(
            create_arguments,
            output,
            error_output,
            create_passwords,
            settings.path()
        ) == 0
    );

    const std::array<std::string_view, 4> select_arguments{
        "wallet", "select", "--file", path
    };
    bbc::app::PasswordReader wrong_password = passwords({"wrong"});
    CHECK(
        bbc::app::run(
            select_arguments,
            output,
            error_output,
            wrong_password,
            settings.path()
        ) == 1
    );
    CHECK_FALSE(std::filesystem::exists(settings.path() / "selected-wallet"));

    std::ostringstream select_output;
    std::ostringstream select_error;
    bbc::app::PasswordReader correct_password = passwords({"secret"});
    CHECK(
        bbc::app::run(
            select_arguments,
            select_output,
            select_error,
            correct_password,
            settings.path()
        ) == 0
    );
    CHECK(select_output.str().find("Selected wallet: ") != std::string::npos);
    CHECK(select_error.str().empty());
}

TEST_CASE("wallet commands explain when no wallet is selected", "[application][wallet]") {
    TemporaryCliSettings settings;
    constexpr std::array<std::string_view, 2> arguments{"wallet", "address"};
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(bbc::app::run(arguments, output, error_output, {}, settings.path()) == 1);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("no wallet is selected") != std::string::npos);
}

TEST_CASE("wallet commands sign and verify an exact message", "[application][wallet]") {
    TemporaryCliWallet wallet_file;
    const std::string path = wallet_file.path().string();
    const std::array<std::string_view, 4> create_arguments{
        "wallet", "create", "--file", path
    };
    std::ostringstream create_output;
    std::ostringstream error_output;
    bbc::app::PasswordReader create_passwords = passwords({"secret", "secret"});
    REQUIRE(
        bbc::app::run(
            create_arguments,
            create_output,
            error_output,
            create_passwords
        ) == 0
    );

    constexpr std::string_view message = "A sends 10 BBC to B";
    const std::array<std::string_view, 6> sign_arguments{
        "wallet", "sign", "--file", path, "--message", message
    };
    std::ostringstream sign_output;
    bbc::app::PasswordReader sign_password = passwords({"secret"});
    REQUIRE(
        bbc::app::run(sign_arguments, sign_output, error_output, sign_password) == 0
    );

    const std::string public_key = output_field(sign_output.str(), "Public key: ");
    const std::string signature = output_field(sign_output.str(), "Signature: ");
    REQUIRE(public_key.size() == 64);
    REQUIRE(signature.size() == 128);

    const std::array<std::string_view, 8> verify_arguments{
        "wallet",
        "verify",
        "--public-key",
        public_key,
        "--message",
        message,
        "--signature",
        signature,
    };
    std::ostringstream verify_output;
    CHECK(bbc::app::run(verify_arguments, verify_output, error_output) == 0);
    CHECK(verify_output.str() == "Signature verification: success\n");

    const std::array<std::string_view, 8> modified_arguments{
        "wallet",
        "verify",
        "--public-key",
        public_key,
        "--message",
        "A sends 100 BBC to B",
        "--signature",
        signature,
    };
    std::ostringstream modified_output;
    std::ostringstream modified_error;
    CHECK(bbc::app::run(modified_arguments, modified_output, modified_error) == 1);
    CHECK(modified_output.str().empty());
    CHECK(modified_error.str() == "Signature verification: failed\n");
}

TEST_CASE("wallet creation rejects mismatched passwords", "[application][wallet]") {
    TemporaryCliWallet wallet_file;
    const std::string path = wallet_file.path().string();
    const std::array<std::string_view, 4> arguments{
        "wallet", "create", "--file", path
    };
    std::ostringstream output;
    std::ostringstream error_output;
    bbc::app::PasswordReader password_reader = passwords({"first", "second"});

    CHECK(bbc::app::run(arguments, output, error_output, password_reader) == 1);
    CHECK_FALSE(std::filesystem::exists(wallet_file.path()));
    CHECK(error_output.str() == "Wallet passwords do not match.\n");
}

TEST_CASE("wallet commands reject missing and secret command-line options", "[application][wallet]") {
    constexpr std::array<std::string_view, 2> missing_file{"wallet", "create"};
    constexpr std::array<std::string_view, 6> password_option{
        "wallet", "create", "--file", "wallet.dat", "--password", "secret"
    };
    std::ostringstream output;
    std::ostringstream missing_error;
    std::ostringstream password_error;

    CHECK(bbc::app::run(missing_file, output, missing_error) == 2);
    CHECK(missing_error.str() == "Missing required option: --file\n");
    CHECK(bbc::app::run(password_option, output, password_error) == 2);
    CHECK(password_error.str() == "Unknown option: --password\n");
}

TEST_CASE("transaction commands create show and verify a binary file", "[application][transaction]") {
    TemporaryCliWallet wallet_file;
    TemporaryCliSettings settings;
    TemporaryCliTransaction transaction_file;
    const std::string wallet_path = wallet_file.path().string();
    const std::string transaction_path = transaction_file.path().string();
    const std::array<std::string_view, 5> wallet_arguments{
        "wallet", "create", "--file", wallet_path, "--load"
    };
    std::ostringstream setup_output;
    std::ostringstream error_output;
    bbc::app::PasswordReader setup_passwords = passwords({"secret", "secret"});
    REQUIRE(
        bbc::app::run(
            wallet_arguments,
            setup_output,
            error_output,
            setup_passwords,
            settings.path()
        ) == 0
    );

    constexpr std::string_view recipient =
        "BBC_000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F";
    const std::array<std::string_view, 12> create_arguments{
        "transaction",
        "create",
        "--to",
        recipient,
        "--amount",
        "1000",
        "--fee",
        "5",
        "--nonce",
        "0",
        "--out",
        transaction_path,
    };
    std::ostringstream create_output;
    bbc::app::PasswordReader transaction_password = passwords({"secret"});
    REQUIRE(
        bbc::app::run(
            create_arguments,
            create_output,
            error_output,
            transaction_password,
            settings.path()
        ) == 0
    );
    REQUIRE(std::filesystem::file_size(transaction_file.path()) == 161);
    CHECK(output_field(create_output.str(), "Transaction ID: ").size() == 64);
    CHECK(output_field(create_output.str(), "Recipient: ") == recipient);

    const std::array<std::string_view, 4> show_arguments{
        "transaction", "show", "--file", transaction_path
    };
    std::ostringstream show_output;
    CHECK(bbc::app::run(show_arguments, show_output, error_output) == 0);
    CHECK(output_field(show_output.str(), "Transaction ID: ") ==
          output_field(create_output.str(), "Transaction ID: "));
    CHECK(show_output.str().find("Signature verification: success") != std::string::npos);

    const std::array<std::string_view, 4> verify_arguments{
        "transaction", "verify", "--file", transaction_path
    };
    std::ostringstream verify_output;
    CHECK(bbc::app::run(verify_arguments, verify_output, error_output) == 0);
    CHECK(verify_output.str().find("Transaction verification: success") !=
          std::string::npos);
    CHECK(error_output.str().empty());
}

TEST_CASE("transaction command rejects invalid input before opening a wallet", "[application][transaction]") {
    constexpr std::array<std::string_view, 12> arguments{
        "transaction",
        "create",
        "--to",
        "not-an-address",
        "--amount",
        "10.5",
        "--fee",
        "0",
        "--nonce",
        "0",
        "--out",
        "transaction.bbctx",
    };
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(bbc::app::run(arguments, output, error_output) == 2);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("unsigned 64-bit decimal integer") !=
          std::string::npos);
}
