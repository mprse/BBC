#include "app/block_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/chain/block.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/storage/chain_store.hpp"
#include "bbc/storage/mempool_store.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/address.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;

std::optional<std::uint64_t> parse_uint64(
    const std::string_view encoded,
    const std::string_view name,
    std::ostream& error_output
) {
    std::uint64_t value = 0;
    const std::from_chars_result result = std::from_chars(
        encoded.data(),
        encoded.data() + encoded.size(),
        value,
        10
    );
    if (encoded.empty() || result.ec != std::errc{} ||
        result.ptr != encoded.data() + encoded.size()) {
        error_output << "Option " << name
                     << " must be an unsigned 64-bit decimal integer.\n";
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> required_uint64_option(
    const CommandOptions& options,
    const std::string_view name,
    std::ostream& error_output
) {
    const std::optional<std::string_view> encoded =
        required_option(options, name, error_output);
    if (!encoded.has_value()) {
        return std::nullopt;
    }
    return parse_uint64(*encoded, name, error_output);
}

std::optional<std::uint64_t> optional_uint64_option(
    const CommandOptions& options,
    const std::string_view name,
    const std::uint64_t default_value,
    std::ostream& error_output
) {
    const std::vector<std::string_view> values = option_values(options, name);
    if (values.empty()) {
        return default_value;
    }
    return parse_uint64(values.front(), name, error_output);
}

void print_block(const chain::Block& block, std::ostream& output) {
    output << "Block ID: " << crypto::to_upper_hex(block.id()) << '\n'
           << "Chain ID: " << block.chain_id() << '\n'
           << "Height: " << block.height() << '\n'
           << "Previous block hash: "
           << crypto::to_upper_hex(block.previous_block_hash()) << '\n'
           << "Transaction root: "
           << crypto::to_upper_hex(block.transaction_root()) << '\n'
           << "Reward recipient: ";
    if (block.reward_recipient().has_value()) {
        output << block.reward_recipient()->value();
    } else {
        output << "none";
    }
    output << '\n'
           << "Timestamp: " << block.timestamp() << '\n'
           << "Difficulty target: "
           << crypto::to_upper_hex(block.difficulty_target()) << '\n'
           << "Mining nonce: " << block.mining_nonce() << '\n'
           << "Transaction count: " << block.transactions().size() << '\n';
    for (std::size_t index = 0; index < block.transactions().size(); ++index) {
        output << "Transaction " << index << " ID: "
               << crypto::to_upper_hex(block.transactions()[index].id()) << '\n';
    }
}

int create_block_file(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(
            options,
            {
                "--height",
                "--previous",
                "--reward-to",
                "--timestamp",
                "--target",
                "--nonce",
                "--transaction",
                "--out",
            },
            error_output
        )) {
        return usage_error;
    }

    const std::optional<std::uint64_t> height =
        required_uint64_option(options, "--height", error_output);
    const std::optional<std::string_view> encoded_previous =
        required_option(options, "--previous", error_output);
    const std::optional<std::string_view> encoded_reward_recipient =
        required_option(options, "--reward-to", error_output);
    const std::optional<std::uint64_t> timestamp =
        required_uint64_option(options, "--timestamp", error_output);
    const std::optional<std::uint64_t> mining_nonce =
        optional_uint64_option(options, "--nonce", 0, error_output);
    const std::optional<std::string_view> output_file =
        required_option(options, "--out", error_output);
    if (!height.has_value() || !encoded_previous.has_value() ||
        !encoded_reward_recipient.has_value() || !timestamp.has_value() ||
        !mining_nonce.has_value() || !output_file.has_value()) {
        return usage_error;
    }

    crypto::Hash256 previous_hash{};
    if (!crypto::decode_hex(*encoded_previous, previous_hash)) {
        error_output << "Previous block hash must contain exactly 64 hexadecimal "
                        "characters.\n";
        return usage_error;
    }
    const std::optional<wallet::Address> reward_recipient =
        wallet::Address::parse(*encoded_reward_recipient);
    if (!reward_recipient.has_value()) {
        error_output << "Reward recipient must be a valid BBC address.\n";
        return usage_error;
    }

    crypto::Hash256 target = consensus::fixed_difficulty_target();
    const std::vector<std::string_view> encoded_targets = option_values(options, "--target");
    if (!encoded_targets.empty() && !crypto::decode_hex(encoded_targets.front(), target)) {
        error_output << "Difficulty target must contain exactly 64 hexadecimal "
                        "characters.\n";
        return usage_error;
    }

    std::vector<transaction::SignedTransaction> transactions;
    const std::vector<std::string_view> transaction_files =
        option_values(options, "--transaction");
    transactions.reserve(transaction_files.size());
    for (const std::string_view file : transaction_files) {
        transaction::TransactionResult loaded =
            transaction::load_transaction(std::filesystem::path{std::string{file}});
        if (!loaded.has_value()) {
            error_output << "Could not read transaction " << file << ": "
                         << transaction::transaction_error_message(loaded.error()) << '\n';
            return runtime_error;
        }
        transactions.push_back(std::move(loaded).value());
    }

    chain::BlockResult created = chain::create_block({
        *height,
        previous_hash,
        *reward_recipient,
        *timestamp,
        target,
        *mining_nonce,
        std::move(transactions),
    });
    if (!created.has_value()) {
        error_output << "Could not create block: "
                     << chain::block_error_message(created.error()) << '\n';
        return usage_error;
    }

    const std::filesystem::path block_path{std::string{*output_file}};
    const chain::BlockError save_error = chain::save_block(created.value(), block_path);
    if (save_error != chain::BlockError::none) {
        error_output << "Could not save block: "
                     << chain::block_error_message(save_error) << '\n';
        return runtime_error;
    }

    output << "Block saved: " << block_path.string() << '\n';
    print_block(created.value(), output);
    return success;
}

int create_block_candidate(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(
            options,
            {"--data-dir", "--reward-to", "--timestamp", "--out"},
            error_output
        )) {
        return usage_error;
    }
    const std::optional<std::string_view> encoded_data_directory =
        required_option(options, "--data-dir", error_output);
    const std::optional<std::string_view> encoded_reward_recipient =
        required_option(options, "--reward-to", error_output);
    const std::optional<std::uint64_t> timestamp =
        required_uint64_option(options, "--timestamp", error_output);
    const std::optional<std::string_view> output_file =
        required_option(options, "--out", error_output);
    if (!encoded_data_directory.has_value() ||
        !encoded_reward_recipient.has_value() || !timestamp.has_value() ||
        !output_file.has_value()) {
        return usage_error;
    }
    const std::optional<wallet::Address> reward_recipient =
        wallet::Address::parse(*encoded_reward_recipient);
    if (!reward_recipient.has_value()) {
        error_output << "Reward recipient must be a valid BBC address.\n";
        return usage_error;
    }

    const std::filesystem::path data_directory{
        std::string{*encoded_data_directory}
    };
    storage::ChainStoreResult chain_store =
        storage::ChainStore::open(data_directory);
    if (!chain_store.has_value()) {
        error_output << "Could not open chain store: "
                     << storage::chain_store_error_message(chain_store.error()) << '\n';
        return runtime_error;
    }
    const chain::Block& tip = chain_store.value().blockchain().tip();
    if (tip.height() == std::numeric_limits<std::uint64_t>::max()) {
        error_output << "Could not create block candidate: chain height is exhausted.\n";
        return runtime_error;
    }
    if (*timestamp <= tip.timestamp()) {
        error_output << "Block timestamp must be greater than the chain tip timestamp.\n";
        return usage_error;
    }
    storage::MempoolStoreResult mempool_store = storage::MempoolStore::open(
        data_directory,
        chain_store.value().blockchain().state()
    );
    if (!mempool_store.has_value()) {
        error_output << "Could not open mempool: "
                     << storage::mempool_store_error_message(mempool_store.error()) << '\n';
        return runtime_error;
    }

    chain::BlockResult created = chain::create_block({
        tip.height() + 1,
        tip.id(),
        *reward_recipient,
        *timestamp,
        consensus::fixed_difficulty_target(),
        0,
        mempool_store.value().mempool().select(
            chain::maximum_transactions_per_block
        ),
    });
    if (!created.has_value()) {
        error_output << "Could not create block candidate: "
                     << chain::block_error_message(created.error()) << '\n';
        return runtime_error;
    }
    const std::filesystem::path block_path{std::string{*output_file}};
    const chain::BlockError save_error = chain::save_block(created.value(), block_path);
    if (save_error != chain::BlockError::none) {
        error_output << "Could not save block candidate: "
                     << chain::block_error_message(save_error) << '\n';
        return runtime_error;
    }
    output << "Block candidate saved: " << block_path.string() << '\n';
    print_block(created.value(), output);
    return success;
}

int show_genesis(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--out"}, error_output)) {
        return usage_error;
    }

    chain::Block genesis = chain::genesis_block();
    const std::vector<std::string_view> output_files = option_values(options, "--out");
    if (!output_files.empty()) {
        const std::filesystem::path path{std::string{output_files.front()}};
        const chain::BlockError save_error = chain::save_block(genesis, path);
        if (save_error != chain::BlockError::none) {
            error_output << "Could not save genesis block: "
                         << chain::block_error_message(save_error) << '\n';
            return runtime_error;
        }
        output << "Genesis block saved: " << path.string() << '\n';
    }
    print_block(genesis, output);
    return success;
}

int show_block_file(
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

    chain::BlockResult loaded =
        chain::load_block(std::filesystem::path{std::string{*file}});
    if (!loaded.has_value()) {
        error_output << "Could not read block: "
                     << chain::block_error_message(loaded.error()) << '\n';
        return runtime_error;
    }
    print_block(loaded.value(), output);
    return success;
}

int verify_block_file(
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

    chain::BlockResult loaded =
        chain::load_block(std::filesystem::path{std::string{*file}});
    if (!loaded.has_value()) {
        error_output << "Block verification: failed: "
                     << chain::block_error_message(loaded.error()) << '\n';
        return runtime_error;
    }
    const consensus::ProofOfWorkError proof_error =
        consensus::validate_proof_of_work(loaded.value());
    if (proof_error != consensus::ProofOfWorkError::none) {
        error_output << "Block verification: failed: "
                     << consensus::proof_of_work_error_message(proof_error) << '\n';
        return runtime_error;
    }

    output << "Block verification: success\n"
           << "Block ID: " << crypto::to_upper_hex(loaded.value().id()) << '\n'
           << "Format and Proof of Work are valid. Chain state is not checked in "
              "this command.\n";
    return success;
}

int mine_block_file(
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    if (!validate_options(options, {"--file", "--out", "--max-attempts"}, error_output)) {
        return usage_error;
    }
    const std::optional<std::string_view> file =
        required_option(options, "--file", error_output);
    const std::optional<std::string_view> output_file =
        required_option(options, "--out", error_output);
    const std::optional<std::uint64_t> maximum_attempts = optional_uint64_option(
        options,
        "--max-attempts",
        std::numeric_limits<std::uint64_t>::max(),
        error_output
    );
    if (!file.has_value() || !output_file.has_value() ||
        !maximum_attempts.has_value()) {
        return usage_error;
    }

    const std::filesystem::path mined_path{std::string{*output_file}};
    std::error_code destination_error;
    if (std::filesystem::exists(mined_path, destination_error)) {
        error_output << "Could not save mined block: "
                     << chain::block_error_message(chain::BlockError::file_already_exists)
                     << '\n';
        return runtime_error;
    }
    if (destination_error) {
        error_output << "Could not save mined block: "
                     << chain::block_error_message(chain::BlockError::io_error) << '\n';
        return runtime_error;
    }

    chain::BlockResult loaded =
        chain::load_block(std::filesystem::path{std::string{*file}});
    if (!loaded.has_value()) {
        error_output << "Could not read mining candidate: "
                     << chain::block_error_message(loaded.error()) << '\n';
        return runtime_error;
    }

    const auto started = std::chrono::steady_clock::now();
    consensus::MiningResult mined =
        consensus::mine_block(loaded.value(), *maximum_attempts);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    if (!mined.has_value()) {
        error_output << "Mining stopped: "
                     << consensus::proof_of_work_error_message(mined.error()) << '\n'
                     << "Attempts: " << mined.attempts() << '\n';
        if (mined.next_nonce().has_value()) {
            error_output << "Next nonce: " << *mined.next_nonce() << '\n';
        }
        return runtime_error;
    }

    const chain::BlockError save_error = chain::save_block(mined.value(), mined_path);
    if (save_error != chain::BlockError::none) {
        error_output << "Could not save mined block: "
                     << chain::block_error_message(save_error) << '\n';
        return runtime_error;
    }

    const double hashes_per_second = elapsed.count() > 0.0
        ? static_cast<double>(mined.attempts()) / elapsed.count()
        : 0.0;
    output << "Mined block saved: " << mined_path.string() << '\n';
    print_block(mined.value(), output);
    output << "Attempts: " << mined.attempts() << '\n'
           << std::fixed << std::setprecision(3)
           << "Elapsed seconds: " << elapsed.count() << '\n'
           << std::setprecision(0)
           << "Hash rate (hashes/s): " << hashes_per_second << '\n';
    return success;
}

}  // namespace

void print_block_help(std::ostream& output) {
    output << "Block commands:\n"
           << "  bbc block genesis [--out <path>]\n"
           << "  bbc block create --height <value> --previous <block-id> "
              "--reward-to <address> --timestamp <unix-seconds> [--target <hex>] "
              "[--nonce <value>] [--transaction <path>]... --out <path>\n"
           << "  bbc block candidate --data-dir <path> --reward-to <address> "
              "--timestamp <unix-seconds> --out <path>\n"
           << "  bbc block show --file <path>\n"
           << "  bbc block verify --file <path>\n"
           << "  bbc block mine --file <candidate> --out <path> "
              "[--max-attempts <value>]\n";
}

int run_block_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_block_help(output);
        return success;
    }

    const std::string_view command = arguments.front();
    const std::optional<CommandOptions> options = parse_options(
        arguments.subspan(1),
        error_output,
        {"--transaction"}
    );
    if (!options.has_value()) {
        return usage_error;
    }

    if (command == "genesis") {
        return show_genesis(*options, output, error_output);
    }
    if (command == "create") {
        return create_block_file(*options, output, error_output);
    }
    if (command == "candidate") {
        return create_block_candidate(*options, output, error_output);
    }
    if (command == "show") {
        return show_block_file(*options, output, error_output);
    }
    if (command == "verify") {
        return verify_block_file(*options, output, error_output);
    }
    if (command == "mine") {
        return mine_block_file(*options, output, error_output);
    }

    error_output << "Unknown block command: " << command << "\n\n";
    print_block_help(error_output);
    return usage_error;
}

}  // namespace bbc::app::detail
