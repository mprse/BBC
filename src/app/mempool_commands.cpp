#include "app/mempool_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/mempool/mempool.hpp"
#include "bbc/storage/chain_store.hpp"
#include "bbc/storage/mempool_store.hpp"
#include "bbc/transaction/transaction.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

struct Stores {
    storage::ChainStore chain;
    storage::MempoolStore mempool;
};

std::optional<Stores> open_stores(
    const std::filesystem::path& data_directory,
    std::ostream& error_output
) {
    storage::ChainStoreResult chain_store = storage::ChainStore::open(data_directory);
    if (!chain_store.has_value()) {
        error_output << "Could not open chain store: "
                     << storage::chain_store_error_message(chain_store.error()) << '\n';
        return std::nullopt;
    }
    storage::MempoolStoreResult mempool_store = storage::MempoolStore::open(
        data_directory,
        chain_store.value().blockchain().state()
    );
    if (!mempool_store.has_value()) {
        error_output << "Could not open mempool: "
                     << storage::mempool_store_error_message(mempool_store.error()) << '\n';
        return std::nullopt;
    }
    return Stores{
        std::move(chain_store).value(),
        std::move(mempool_store).value(),
    };
}

std::optional<std::filesystem::path> data_directory(
    const CommandOptions& options,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--data-dir"}, error_output)) {
        return std::nullopt;
    }
    const std::optional<std::string_view> value =
        required_option(options, "--data-dir", error_output);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::filesystem::path{std::string{*value}};
}

int add_transaction(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--data-dir", "--transaction"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> encoded_directory =
        required_option(options, "--data-dir", error_output);
    const std::optional<std::string_view> encoded_transaction =
        required_option(options, "--transaction", error_output);
    if (!encoded_directory.has_value() || !encoded_transaction.has_value()) {
        return usage_error;
    }
    const std::filesystem::path directory{std::string{*encoded_directory}};
    std::optional<Stores> stores = open_stores(directory, error_output);
    if (!stores.has_value()) {
        return runtime_error;
    }
    transaction::TransactionResult loaded = transaction::load_transaction(
        std::filesystem::path{std::string{*encoded_transaction}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not load transaction: "
                     << transaction::transaction_error_message(loaded.error()) << '\n';
        return runtime_error;
    }
    const crypto::Hash256 id = loaded.value().id();
    const storage::MempoolStoreAddResult added =
        stores->mempool.add(std::move(loaded).value());
    if (added.storage_error != storage::MempoolStoreError::none) {
        error_output << "Could not persist transaction in mempool: "
                     << storage::mempool_store_error_message(added.storage_error) << '\n';
        return runtime_error;
    }
    if (added.mempool_error != mempool::MempoolError::none) {
        error_output << "Transaction rejected by mempool: "
                     << mempool::mempool_error_message(added.mempool_error) << '\n';
        return runtime_error;
    }
    output << "Transaction added to mempool\n"
           << "Transaction ID: " << crypto::to_upper_hex(id) << '\n'
           << "Pending transactions: " << stores->mempool.mempool().size() << '\n';
    return success;
}

int list_transactions(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    const std::optional<std::filesystem::path> directory =
        data_directory(options, error_output);
    if (!directory.has_value()) {
        return usage_error;
    }
    std::optional<Stores> stores = open_stores(*directory, error_output);
    if (!stores.has_value()) {
        return runtime_error;
    }
    const auto& pending = stores->mempool.mempool().transactions();
    output << "Pending transactions: " << pending.size() << '\n';
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const transaction::SignedTransaction& value = pending[index];
        output << "Transaction " << index << " ID: "
               << crypto::to_upper_hex(value.id()) << '\n'
               << "  Sender: " << value.sender().value() << '\n'
               << "  Recipient: " << value.recipient().value() << '\n'
               << "  Amount (base units): " << value.amount() << '\n'
               << "  Fee (base units): " << value.fee() << '\n'
               << "  Account nonce: " << value.account_nonce() << '\n';
    }
    return success;
}

int show_status(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    const std::optional<std::filesystem::path> directory =
        data_directory(options, error_output);
    if (!directory.has_value()) {
        return usage_error;
    }
    std::optional<Stores> stores = open_stores(*directory, error_output);
    if (!stores.has_value()) {
        return runtime_error;
    }
    output << "Pending transactions: " << stores->mempool.mempool().size() << '\n'
           << "Maximum transactions: " << mempool::maximum_mempool_transactions << '\n'
           << "Chain height: " << stores->chain.blockchain().tip().height() << '\n'
           << "Database: " << stores->mempool.database_path().string() << '\n';
    return success;
}

}  // namespace

void print_mempool_help(std::ostream& output) {
    output << "Mempool commands:\n"
           << "  bbc mempool add --data-dir <path> --transaction <path>\n"
           << "  bbc mempool list --data-dir <path>\n"
           << "  bbc mempool status --data-dir <path>\n";
}

int run_mempool_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_mempool_help(output);
        return success;
    }
    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }
    if (command == "add") {
        return add_transaction(*options, output, error_output);
    }
    if (command == "list") {
        return list_transactions(*options, output, error_output);
    }
    if (command == "status") {
        return show_status(*options, output, error_output);
    }
    error_output << "Unknown mempool command: " << command << "\n\n";
    print_mempool_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
