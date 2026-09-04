#include "bbc/app/application.hpp"

#include "bbc/core/version.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/crypto/keys.hpp"
#include "bbc/wallet/wallet.hpp"
#include "app/block_commands.hpp"
#include "app/chain_commands.hpp"
#include "app/command_options.hpp"
#include "app/transaction_commands.hpp"
#include "app/wallet_access.hpp"
#include "app/wallet_selection.hpp"
#include "crypto/secure_memory.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace bbc::app {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

using Options = detail::CommandOptions;
using detail::has_option;
using detail::parse_options;
using detail::required_option;
using detail::validate_options;

class SensitiveString final {
public:
    SensitiveString() = default;
    SensitiveString(const SensitiveString&) = delete;
    SensitiveString& operator=(const SensitiveString&) = delete;
    SensitiveString(SensitiveString&&) = delete;
    SensitiveString& operator=(SensitiveString&&) = delete;

    ~SensitiveString() {
        crypto::detail::secure_clear(value_);
    }

    [[nodiscard]] std::string& mutable_value() noexcept {
        return value_;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return value_;
    }

private:
    std::string value_;
};

crypto::ByteView as_bytes(const std::string_view text) {
    return {
        reinterpret_cast<const crypto::Byte*>(text.data()),
        text.size(),
    };
}

void print_wallet_help(std::ostream& output) {
    output << "Wallet commands:\n"
           << "  bbc wallet create --file <path> [--load]\n"
           << "  bbc wallet select --file <path>\n"
           << "  bbc wallet selected\n"
           << "  bbc wallet address [--file <path>]\n"
           << "  bbc wallet balance [--file <path>] [--block <path>]...\n"
           << "  bbc wallet sign [--file <path>] --message <text>\n"
           << "  bbc wallet verify --public-key <hex> --message <text> "
              "--signature <hex>\n";
}

void print_help(std::ostream& output) {
    output << "Bi-Bi-Coin (BBC)\n\n"
           << "Usage:\n"
           << "  bbc <command>\n\n"
           << "Commands:\n"
           << "  help         Show this help message\n"
           << "  version      Show the application version\n"
           << "  wallet       Create, inspect, sign, or verify with a wallet\n"
           << "  transaction  Create, inspect, or verify a signed transaction\n"
           << "  block        Create, mine, inspect, or verify a block\n"
           << "  chain        Verify blocks and inspect chain state\n"
           << "  wallet-demo  Generate a temporary wallet and verify a signature\n\n";
    print_wallet_help(output);
    output << '\n';
    detail::print_transaction_help(output);
    output << '\n';
    detail::print_block_help(output);
    output << '\n';
    detail::print_chain_help(output);
}

bool read_password(
    const PasswordReader& password_reader,
    const std::string_view prompt,
    SensitiveString& password,
    std::ostream& error_output
) {
    if (!password_reader || !password_reader(prompt, password.mutable_value())) {
        error_output << "Could not read a password securely from this terminal.\n";
        return false;
    }
    return true;
}

int create_wallet(
    const Options& options,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file", "--load"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> file =
        required_option(options, "--file", error_output);
    if (!file.has_value()) {
        return usage_error;
    }

    SensitiveString password;
    SensitiveString confirmation;
    if (!read_password(password_reader, "New wallet password: ", password, error_output) ||
        !read_password(
            password_reader,
            "Confirm wallet password: ",
            confirmation,
            error_output
        )) {
        return runtime_error;
    }
    if (password.view().empty()) {
        error_output << "Wallet password must not be empty.\n";
        return runtime_error;
    }
    if (password.view() != confirmation.view()) {
        error_output << "Wallet passwords do not match.\n";
        return runtime_error;
    }

    wallet::Wallet new_wallet = wallet::Wallet::create();
    const std::filesystem::path wallet_path{std::string{*file}};
    const wallet::WalletFileError save_error = new_wallet.save(wallet_path, password.view());
    if (save_error != wallet::WalletFileError::none) {
        error_output << "Could not create wallet: "
                     << wallet::wallet_file_error_message(save_error) << '\n';
        return runtime_error;
    }

    output << "Wallet created: " << wallet_path.string() << '\n'
           << "Address: " << new_wallet.address().value() << '\n'
           << "Public key: " << crypto::to_upper_hex(new_wallet.public_key()) << '\n';

    if (has_option(options, "--load")) {
        const detail::WalletSelectionError selection_error =
            detail::select_wallet(settings_directory, wallet_path);
        if (selection_error != detail::WalletSelectionError::none) {
            error_output << "Wallet was created but could not be selected: "
                         << detail::wallet_selection_error_message(selection_error) << '\n';
            return runtime_error;
        }
        const detail::WalletSelectionResult selection =
            detail::selected_wallet(settings_directory);
        const auto* selected_path = std::get_if<std::filesystem::path>(&selection);
        if (selected_path == nullptr) {
            error_output << "Wallet was created but the selection could not be read.\n";
            return runtime_error;
        }
        output << "Selected wallet: " << selected_path->string() << '\n';
    }
    return success;
}

int select_wallet_file(
    const Options& options,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> file =
        required_option(options, "--file", error_output);
    if (!file.has_value()) {
        return usage_error;
    }

    const std::filesystem::path wallet_path{std::string{*file}};
    std::optional<wallet::Wallet> loaded_wallet = detail::open_wallet(
        wallet_path,
        password_reader,
        error_output
    );
    if (!loaded_wallet.has_value()) {
        return runtime_error;
    }

    const detail::WalletSelectionError selection_error =
        detail::select_wallet(settings_directory, wallet_path);
    if (selection_error != detail::WalletSelectionError::none) {
        error_output << "Could not select wallet: "
                     << detail::wallet_selection_error_message(selection_error) << '\n';
        return runtime_error;
    }

    const detail::WalletSelectionResult selection = detail::selected_wallet(settings_directory);
    const auto* selected_path = std::get_if<std::filesystem::path>(&selection);
    if (selected_path == nullptr) {
        error_output << "Wallet was selected but the selection could not be read.\n";
        return runtime_error;
    }
    output << "Selected wallet: " << selected_path->string() << '\n'
           << "Address: " << loaded_wallet->address().value() << '\n';
    return success;
}

int show_selected_wallet(
    const Options& options,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {}, error_output)) {
        return usage_error;
    }
    const detail::WalletSelectionResult selection = detail::selected_wallet(settings_directory);
    if (const auto* error = std::get_if<detail::WalletSelectionError>(&selection)) {
        error_output << "Could not resolve wallet: "
                     << detail::wallet_selection_error_message(*error) << '\n';
        return runtime_error;
    }
    output << "Selected wallet: "
           << std::get<std::filesystem::path>(selection).string() << '\n';
    return success;
}

int show_wallet_address(
    const Options& options,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::filesystem::path> file =
        detail::resolve_wallet_path(options, settings_directory, "--file", error_output);
    if (!file.has_value()) {
        return runtime_error;
    }

    std::optional<wallet::Wallet> loaded_wallet = detail::open_wallet(
        *file,
        password_reader,
        error_output
    );
    if (!loaded_wallet.has_value()) {
        return runtime_error;
    }

    output << "Address: " << loaded_wallet->address().value() << '\n'
           << "Public key: " << crypto::to_upper_hex(loaded_wallet->public_key()) << '\n';
    return success;
}

int show_wallet_balance(
    const Options& options,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file", "--block"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::filesystem::path> file =
        detail::resolve_wallet_path(options, settings_directory, "--file", error_output);
    if (!file.has_value()) {
        return runtime_error;
    }
    std::optional<wallet::Wallet> loaded_wallet = detail::open_wallet(
        *file,
        password_reader,
        error_output
    );
    if (!loaded_wallet.has_value()) {
        return runtime_error;
    }
    return detail::show_chain_balance(
        loaded_wallet->address(),
        detail::option_values(options, "--block"),
        output,
        error_output
    );
}

int sign_message(
    const Options& options,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file", "--message"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> message =
        required_option(options, "--message", error_output);
    if (!message.has_value()) {
        return usage_error;
    }

    const std::optional<std::filesystem::path> file =
        detail::resolve_wallet_path(options, settings_directory, "--file", error_output);
    if (!file.has_value()) {
        return runtime_error;
    }

    std::optional<wallet::Wallet> loaded_wallet = detail::open_wallet(
        *file,
        password_reader,
        error_output
    );
    if (!loaded_wallet.has_value()) {
        return runtime_error;
    }

    const crypto::Signature signature = loaded_wallet->sign(as_bytes(*message));
    output << "Address: " << loaded_wallet->address().value() << '\n'
           << "Public key: " << crypto::to_upper_hex(loaded_wallet->public_key()) << '\n'
           << "Signature: " << crypto::to_upper_hex(signature) << '\n';
    return success;
}

int verify_message(
    const Options& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(
            options,
            {"--public-key", "--message", "--signature"},
            error_output
        )) {
        return usage_error;
    }
    const std::optional<std::string_view> encoded_public_key =
        required_option(options, "--public-key", error_output);
    const std::optional<std::string_view> message =
        required_option(options, "--message", error_output);
    const std::optional<std::string_view> encoded_signature =
        required_option(options, "--signature", error_output);
    if (!encoded_public_key.has_value() || !message.has_value() ||
        !encoded_signature.has_value()) {
        return usage_error;
    }

    crypto::PublicKey public_key{};
    crypto::Signature signature{};
    if (!crypto::decode_hex(*encoded_public_key, public_key)) {
        error_output << "Public key must contain exactly 64 hexadecimal characters.\n";
        return usage_error;
    }
    if (!crypto::decode_hex(*encoded_signature, signature)) {
        error_output << "Signature must contain exactly 128 hexadecimal characters.\n";
        return usage_error;
    }

    if (!crypto::verify_signature(as_bytes(*message), signature, public_key)) {
        error_output << "Signature verification: failed\n";
        return runtime_error;
    }

    output << "Signature verification: success\n";
    return success;
}

int run_wallet_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_wallet_help(output);
        return success;
    }

    const std::string_view command = arguments.front();
    const std::optional<Options> options = command == "balance"
        ? parse_options(arguments.subspan(1), error_output, {"--block"})
        : parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }

    if (command == "create") {
        return create_wallet(
            *options,
            password_reader,
            settings_directory,
            output,
            error_output
        );
    }
    if (command == "select") {
        return select_wallet_file(
            *options,
            password_reader,
            settings_directory,
            output,
            error_output
        );
    }
    if (command == "selected") {
        return show_selected_wallet(*options, settings_directory, output, error_output);
    }
    if (command == "address") {
        return show_wallet_address(
            *options,
            password_reader,
            settings_directory,
            output,
            error_output
        );
    }
    if (command == "balance") {
        return show_wallet_balance(
            *options,
            password_reader,
            settings_directory,
            output,
            error_output
        );
    }
    if (command == "sign") {
        return sign_message(
            *options,
            password_reader,
            settings_directory,
            output,
            error_output
        );
    }
    if (command == "verify") {
        return verify_message(*options, output, error_output);
    }

    error_output << "Unknown wallet command: " << command << "\n\n";
    print_wallet_help(error_output);
    return usage_error;
}

int run_wallet_demo(std::ostream& output, std::ostream& error_output) {
    constexpr std::string_view message = "BBC Stage 1 signature demo";
    wallet::Wallet demo_wallet = wallet::Wallet::create();
    const crypto::Signature signature = demo_wallet.sign(as_bytes(message));
    if (!crypto::verify_signature(as_bytes(message), signature, demo_wallet.public_key())) {
        error_output << "Signature verification failed.\n";
        return runtime_error;
    }

    output << "Wallet address: " << demo_wallet.address().value() << '\n'
           << "Ed25519 signature verification: success\n";
    return success;
}

}  // namespace

int run(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory
) {
    if (arguments.empty() || arguments.front() == "help" ||
        arguments.front() == "-h" || arguments.front() == "--help") {
        print_help(output);
        return success;
    }

    if (arguments.front() == "version" || arguments.front() == "--version") {
        output << "BBC " << core::version() << '\n';
        return success;
    }

    if (arguments.front() == "wallet") {
        const std::filesystem::path effective_settings_directory =
            settings_directory.empty() ? detail::default_settings_directory()
                                       : settings_directory;
        return run_wallet_command(
            arguments.subspan(1),
            output,
            error_output,
            password_reader,
            effective_settings_directory
        );
    }

    if (arguments.front() == "transaction") {
        const std::filesystem::path effective_settings_directory =
            settings_directory.empty() ? detail::default_settings_directory()
                                       : settings_directory;
        return detail::run_transaction_command(
            arguments.subspan(1),
            output,
            error_output,
            password_reader,
            effective_settings_directory
        );
    }

    if (arguments.front() == "block") {
        return detail::run_block_command(
            arguments.subspan(1),
            output,
            error_output
        );
    }

    if (arguments.front() == "chain") {
        return detail::run_chain_command(
            arguments.subspan(1),
            output,
            error_output
        );
    }

    if (arguments.front() == "wallet-demo") {
        return run_wallet_demo(output, error_output);
    }

    error_output << "Unknown command: " << arguments.front() << "\n\n";
    print_help(error_output);
    return usage_error;
}

}  // namespace bbc::app
