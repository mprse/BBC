#include "app/chain_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/chain/blockchain.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/storage/chain_store.hpp"
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

bool validate_chain_source(
    const CommandOptions& options,
    std::ostream& error_output
) {
    if (has_option(options, "--data-dir") && has_option(options, "--block")) {
        error_output << "Use either --data-dir or --block, not both.\n";
        return false;
    }
    return true;
}

std::optional<chain::Blockchain> load_chain(
    const CommandOptions& options,
    std::ostream& error_output
) {
    const std::vector<std::string_view> data_directories =
        option_values(options, "--data-dir");
    if (!data_directories.empty()) {
        storage::ChainStoreResult opened = storage::ChainStore::open(
            std::filesystem::path{std::string{data_directories.front()}}
        );
        if (!opened.has_value()) {
            error_output << "Could not open chain store: "
                         << storage::chain_store_error_message(opened.error()) << '\n';
            return std::nullopt;
        }
        return opened.value().blockchain();
    }

    chain::Blockchain blockchain;
    if (!replay_blocks(option_values(options, "--block"), blockchain, error_output)) {
        return std::nullopt;
    }
    return blockchain;
}

void print_append_error(
    const chain::AppendResult& appended,
    std::ostream& error_output
) {
    error_output << chain::chain_error_message(appended.error);
    if (appended.state_error != chain::StateTransitionError::none) {
        error_output << ": "
                     << chain::state_transition_error_message(appended.state_error);
    }
    if (appended.transaction_index.has_value()) {
        error_output << " (transaction index " << *appended.transaction_index << ')';
    }
}

int initialize_chain_store(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--data-dir"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> data_directory =
        required_option(options, "--data-dir", error_output);
    if (!data_directory.has_value()) {
        return usage_error;
    }
    storage::ChainStoreResult initialized = storage::ChainStore::initialize(
        std::filesystem::path{std::string{*data_directory}}
    );
    if (!initialized.has_value()) {
        error_output << "Could not initialize chain store: "
                     << storage::chain_store_error_message(initialized.error()) << '\n';
        return runtime_error;
    }
    output << "Chain store initialized: "
           << initialized.value().data_directory().string() << '\n'
           << "Blocks file: " << initialized.value().blocks_path().string() << '\n'
           << "Index database: "
           << initialized.value().database_path().string() << '\n'
           << "Tip height: " << initialized.value().blockchain().tip().height() << '\n'
           << "Tip block ID: "
           << crypto::to_upper_hex(initialized.value().blockchain().tip().id()) << '\n';
    return success;
}

int add_block_to_store(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--data-dir", "--block"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> data_directory =
        required_option(options, "--data-dir", error_output);
    const std::optional<std::string_view> block_file =
        required_option(options, "--block", error_output);
    if (!data_directory.has_value() || !block_file.has_value()) {
        return usage_error;
    }

    storage::ChainStoreResult opened = storage::ChainStore::open(
        std::filesystem::path{std::string{*data_directory}}
    );
    if (!opened.has_value()) {
        error_output << "Could not open chain store: "
                     << storage::chain_store_error_message(opened.error()) << '\n';
        return runtime_error;
    }
    chain::BlockResult loaded = chain::load_block(
        std::filesystem::path{std::string{*block_file}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not load block: "
                     << chain::block_error_message(loaded.error()) << '\n';
        return runtime_error;
    }

    const crypto::Hash256 block_id = loaded.value().id();
    const std::uint64_t block_height = loaded.value().height();
    const storage::ChainStoreAppendResult appended =
        opened.value().append(std::move(loaded).value());
    if (appended.storage_error != storage::ChainStoreError::none) {
        error_output << "Could not persist block: "
                     << storage::chain_store_error_message(appended.storage_error) << '\n';
        return runtime_error;
    }
    if (!appended.chain_result.has_value()) {
        error_output << "Could not append block: ";
        print_append_error(appended.chain_result, error_output);
        error_output << '\n';
        return runtime_error;
    }

    output << "Block added to chain store\n"
           << "Height: " << block_height << '\n'
           << "Block ID: " << crypto::to_upper_hex(block_id) << '\n'
           << "Chain height: "
           << opened.value().blockchain().tip().height() << '\n';
    return success;
}

int verify_chain(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--block", "--data-dir"}, error_output) ||
        !validate_chain_source(options, error_output)) {
        return usage_error;
    }
    const std::optional<chain::Blockchain> blockchain =
        load_chain(options, error_output);
    if (!blockchain.has_value()) {
        return runtime_error;
    }
    output << "Chain verification: success\n"
           << "Tip height: " << blockchain->tip().height() << '\n'
           << "Tip block ID: "
           << crypto::to_upper_hex(blockchain->tip().id()) << '\n'
           << "Blocks including Genesis: " << blockchain->block_count() << '\n'
           << "Accounts: " << blockchain->state().account_count() << '\n';
    return success;
}

int show_tip(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--block", "--data-dir"}, error_output) ||
        !validate_chain_source(options, error_output)) {
        return usage_error;
    }
    const std::optional<chain::Blockchain> blockchain =
        load_chain(options, error_output);
    if (!blockchain.has_value()) {
        return runtime_error;
    }
    output << "Height: " << blockchain->tip().height() << '\n'
           << "Block ID: " << crypto::to_upper_hex(blockchain->tip().id()) << '\n'
           << "Timestamp: " << blockchain->tip().timestamp() << '\n';
    return success;
}

int show_balance(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(
            options,
            {"--address", "--block", "--data-dir"},
            error_output
        ) ||
        !validate_chain_source(options, error_output)) {
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
        has_option(options, "--data-dir")
            ? std::optional<std::filesystem::path>{std::filesystem::path{
                  std::string{option_values(options, "--data-dir").front()}
              }}
            : std::nullopt,
        output,
        error_output
    );
}

}  // namespace

int show_chain_balance(
    const wallet::Address& address,
    const std::vector<std::string_view>& block_files,
    const std::optional<std::filesystem::path>& data_directory,
    std::ostream& output,
    std::ostream& error_output
) {
    chain::Blockchain blockchain;
    if (data_directory.has_value()) {
        storage::ChainStoreResult opened = storage::ChainStore::open(*data_directory);
        if (!opened.has_value()) {
            error_output << "Could not open chain store: "
                         << storage::chain_store_error_message(opened.error()) << '\n';
            return runtime_error;
        }
        blockchain = opened.value().blockchain();
    } else if (!replay_blocks(block_files, blockchain, error_output)) {
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
              "[--block <path>]...\n"
           << "  bbc chain init --data-dir <path>\n"
           << "  bbc chain add --data-dir <path> --block <path>\n"
           << "  bbc chain verify --data-dir <path>\n"
           << "  bbc chain tip --data-dir <path>\n"
           << "  bbc chain balance --address <BBC-address> --data-dir <path>\n\n"
           << "Use either a persistent data directory or ordered block files.\n";
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
    const bool allows_repeated_blocks =
        command == "verify" || command == "tip" || command == "balance";
    const std::optional<CommandOptions> options = allows_repeated_blocks
        ? parse_options(arguments.subspan(1), error_output, {"--block"})
        : parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }
    if (command == "init") {
        return initialize_chain_store(*options, output, error_output);
    }
    if (command == "add") {
        return add_block_to_store(*options, output, error_output);
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
