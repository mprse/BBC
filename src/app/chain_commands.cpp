#include "app/chain_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/chain/blockchain.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/wallet/address.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

bool replay_blocks(
    const std::vector<std::string_view>& block_files,
    chain::Blockchain& blockchain,
    std::ostream& error_output
) {
    for (const std::string_view encoded_path : block_files) {
        const std::filesystem::path path{std::string{encoded_path}};
        chain::BlockResult loaded = chain::load_block(path);
        if (!loaded.has_value()) {
            error_output << "Could not load block " << path.string() << ": "
                         << chain::block_error_message(loaded.error()) << '\n';
            return false;
        }

        const chain::AppendResult appended =
            blockchain.append(std::move(loaded).value());
        if (!appended.has_value()) {
            error_output << "Could not append block " << path.string() << ": "
                         << chain::chain_error_message(appended.error);
            if (appended.state_error != chain::StateTransitionError::none) {
                error_output << ": "
                             << chain::state_transition_error_message(
                                    appended.state_error
                                );
            }
            if (appended.transaction_index.has_value()) {
                error_output << " (transaction index "
                             << *appended.transaction_index << ')';
            }
            error_output << '\n';
            return false;
        }
    }
    return true;
}

int verify_chain(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--block"}, error_output)) {
        return usage_error;
    }
    chain::Blockchain blockchain;
    if (!replay_blocks(option_values(options, "--block"), blockchain, error_output)) {
        return runtime_error;
    }
    output << "Chain verification: success\n"
           << "Tip height: " << blockchain.tip().height() << '\n'
           << "Tip block ID: "
           << crypto::to_upper_hex(blockchain.tip().id()) << '\n'
           << "Blocks including Genesis: " << blockchain.block_count() << '\n'
           << "Accounts: " << blockchain.state().account_count() << '\n';
    return success;
}

int show_tip(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--block"}, error_output)) {
        return usage_error;
    }
    chain::Blockchain blockchain;
    if (!replay_blocks(option_values(options, "--block"), blockchain, error_output)) {
        return runtime_error;
    }
    output << "Height: " << blockchain.tip().height() << '\n'
           << "Block ID: " << crypto::to_upper_hex(blockchain.tip().id()) << '\n'
           << "Timestamp: " << blockchain.tip().timestamp() << '\n';
    return success;
}

int show_balance(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--address", "--block"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> encoded_address =
        required_option(options, "--address", error_output);
    if (!encoded_address.has_value()) {
        return usage_error;
    }
    const std::optional<wallet::Address> address =
        wallet::Address::parse(*encoded_address);
    if (!address.has_value()) {
        error_output << "Address must be a valid BBC address.\n";
        return usage_error;
    }

    return show_chain_balance(
        *address,
        option_values(options, "--block"),
        output,
        error_output
    );
}

}  // namespace

int show_chain_balance(
    const wallet::Address& address,
    const std::vector<std::string_view>& block_files,
    std::ostream& output,
    std::ostream& error_output
) {
    chain::Blockchain blockchain;
    if (!replay_blocks(block_files, blockchain, error_output)) {
        return runtime_error;
    }
    const chain::AccountState account = blockchain.state().account(address);
    output << "Address: " << address.value() << '\n'
           << "Balance (base units): " << account.balance << '\n'
           << "Next account nonce: " << account.next_nonce << '\n'
           << "Chain height: " << blockchain.tip().height() << '\n';
    return success;
}

void print_chain_help(std::ostream& output) {
    output << "Chain commands:\n"
           << "  bbc chain verify [--block <path>]...\n"
           << "  bbc chain tip [--block <path>]...\n"
           << "  bbc chain balance --address <BBC-address> "
              "[--block <path>]...\n\n"
           << "Block files must be supplied in height order after Genesis.\n";
}

int run_chain_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_chain_help(output);
        return success;
    }

    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output, {"--block"});
    if (!options.has_value()) {
        return usage_error;
    }
    if (command == "verify") {
        return verify_chain(*options, output, error_output);
    }
    if (command == "tip") {
        return show_tip(*options, output, error_output);
    }
    if (command == "balance") {
        return show_balance(*options, output, error_output);
    }

    error_output << "Unknown chain command: " << command << "\n\n";
    print_chain_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
