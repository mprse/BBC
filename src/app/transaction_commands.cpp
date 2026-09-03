#include "app/transaction_commands.hpp"

#include "app/command_options.hpp"
#include "app/wallet_access.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

std::optional<std::uint64_t> parse_uint64_option(
    const CommandOptions& options,
    const std::string_view name,
    std::ostream& error_output
) {
    const std::optional<std::string_view> encoded =
        required_option(options, name, error_output);
    if (!encoded.has_value()) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    const std::from_chars_result result = std::from_chars(
        encoded->data(),
        encoded->data() + encoded->size(),
        value,
        10
    );
    if (encoded->empty() || result.ec != std::errc{} ||
        result.ptr != encoded->data() + encoded->size()) {
        error_output << "Option " << name
                     << " must be an unsigned 64-bit decimal integer.\n";
        return std::nullopt;
    }
    return value;
}

void print_transaction(
    const transaction::SignedTransaction& value,
    std::ostream& output
) {
    output << "Transaction ID: " << crypto::to_upper_hex(value.id()) << '\n'
           << "Chain ID: " << value.chain_id() << '\n'
           << "Sender: " << value.sender().value() << '\n'
           << "Recipient: " << value.recipient().value() << '\n'
           << "Amount (base units): " << value.amount() << '\n'
           << "Fee (base units): " << value.fee() << '\n'
           << "Account nonce: " << value.account_nonce() << '\n'
           << "Public key: " << crypto::to_upper_hex(value.sender_public_key()) << '\n'
           << "Signature: " << crypto::to_upper_hex(value.signature()) << '\n';
}

int create_transaction(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory
) {
    if (!validate_options(
            options,
            {"--to", "--amount", "--fee", "--nonce", "--out", "--wallet"},
            error_output
        )) {
        return usage_error;
    }

    const std::optional<std::string_view> encoded_recipient =
        required_option(options, "--to", error_output);
    const std::optional<std::uint64_t> amount =
        parse_uint64_option(options, "--amount", error_output);
    const std::optional<std::uint64_t> fee =
        parse_uint64_option(options, "--fee", error_output);
    const std::optional<std::uint64_t> account_nonce =
        parse_uint64_option(options, "--nonce", error_output);
    const std::optional<std::string_view> output_file =
        required_option(options, "--out", error_output);
    if (!encoded_recipient.has_value() || !amount.has_value() || !fee.has_value() ||
        !account_nonce.has_value() || !output_file.has_value()) {
        return usage_error;
    }

    const std::optional<wallet::Address> recipient =
        wallet::Address::parse(*encoded_recipient);
    if (!recipient.has_value()) {
        error_output << "Recipient must be a valid BBC address.\n";
        return usage_error;
    }

    const std::optional<std::filesystem::path> wallet_path = resolve_wallet_path(
        options,
        settings_directory,
        "--wallet",
        error_output
    );
    if (!wallet_path.has_value()) {
        return runtime_error;
    }
    std::optional<wallet::Wallet> signer =
        open_wallet(*wallet_path, password_reader, error_output);
    if (!signer.has_value()) {
        return runtime_error;
    }

    transaction::TransactionResult signed_result = transaction::sign_transaction(
        *signer,
        {*recipient, *amount, *fee, *account_nonce}
    );
    if (!signed_result.has_value()) {
        error_output << "Could not create transaction: "
                     << transaction::transaction_error_message(signed_result.error())
                     << '\n';
        return usage_error;
    }

    const std::filesystem::path transaction_path{std::string{*output_file}};
    const transaction::TransactionError save_error = transaction::save_transaction(
        signed_result.value(),
        transaction_path
    );
    if (save_error != transaction::TransactionError::none) {
        error_output << "Could not save transaction: "
                     << transaction::transaction_error_message(save_error) << '\n';
        return runtime_error;
    }

    output << "Transaction saved: " << transaction_path.string() << '\n';
    print_transaction(signed_result.value(), output);
    return success;
}

int show_transaction(
    const CommandOptions& options,
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

    transaction::TransactionResult loaded = transaction::load_transaction(
        std::filesystem::path{std::string{*file}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not read transaction: "
                     << transaction::transaction_error_message(loaded.error()) << '\n';
        return runtime_error;
    }

    print_transaction(loaded.value(), output);
    output << "Signature verification: success\n";
    return success;
}

int verify_transaction_file(
    const CommandOptions& options,
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

    transaction::TransactionResult loaded = transaction::load_transaction(
        std::filesystem::path{std::string{*file}}
    );
    if (!loaded.has_value()) {
        error_output << "Transaction verification: failed: "
                     << transaction::transaction_error_message(loaded.error()) << '\n';
        return runtime_error;
    }

    output << "Transaction verification: success\n"
           << "Transaction ID: " << crypto::to_upper_hex(loaded.value().id()) << '\n';
    return success;
}

}  // namespace

void print_transaction_help(std::ostream& output) {
    output << "Transaction commands:\n"
           << "  bbc transaction create --to <address> --amount <units> --fee <units> "
              "--nonce <value> --out <path> [--wallet <path>]\n"
           << "  bbc transaction show --file <path>\n"
           << "  bbc transaction verify --file <path>\n";
}

int run_transaction_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_transaction_help(output);
        return success;
    }

    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }

    if (command == "create") {
        return create_transaction(
            *options,
            output,
            error_output,
            password_reader,
            settings_directory
        );
    }
    if (command == "show") {
        return show_transaction(*options, output, error_output);
    }
    if (command == "verify") {
        return verify_transaction_file(*options, output, error_output);
    }

    error_output << "Unknown transaction command: " << command << "\n\n";
    print_transaction_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
