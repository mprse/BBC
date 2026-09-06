#include "app/rpc_commands.hpp"

#include "app/command_options.hpp"
#include "bbc/config/application_config.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/rpc/token.hpp"
#include "bbc/transaction/transaction.hpp"

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>

namespace bbc::app::detail {
namespace {

constexpr int success = 0;
constexpr int runtime_error = 1;
constexpr int usage_error = 2;
constexpr std::size_t maximum_rpc_response_size = 64 * 1024;

std::optional<nlohmann::json> call_rpc(
    const config::RpcSettings& rpc,
    const std::string_view token,
    const std::string_view method,
    nlohmann::json params,
    std::ostream& error_output
) {
    nlohmann::json request{
        {"id", 1},
        {"method", method},
        {"token", token},
    };
    if (!params.is_null()) {
        request["params"] = std::move(params);
    }

    try {
        asio::io_context context;
        asio::ip::tcp::socket socket{context};
        socket.connect({
            asio::ip::make_address_v4(rpc.listen.host),
            rpc.listen.port,
        });
        const std::string encoded_request = request.dump() + '\n';
        asio::write(socket, asio::buffer(encoded_request));

        asio::streambuf buffer(maximum_rpc_response_size);
        socket.non_blocking(true);
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{5};
        for (;;) {
            asio::error_code read_error;
            asio::read_until(socket, buffer, '\n', read_error);
            if (!read_error) {
                break;
            }
            if (read_error != asio::error::would_block &&
                read_error != asio::error::try_again) {
                throw asio::system_error{read_error};
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                error_output << "Local RPC response timed out.\n";
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        std::istream input{&buffer};
        std::string line;
        std::getline(input, line);
        nlohmann::json response = nlohmann::json::parse(line);
        if (!response.is_object() || !response.contains("id") ||
            response["id"] != 1 || !response.contains("ok") ||
            !response["ok"].is_boolean()) {
            error_output << "Node returned an invalid RPC response.\n";
            return std::nullopt;
        }
        if (!response["ok"].get<bool>()) {
            if (!response.contains("error") || !response["error"].is_object() ||
                !response["error"].contains("code") ||
                !response["error"]["code"].is_string() ||
                !response["error"].contains("message") ||
                !response["error"]["message"].is_string()) {
                error_output << "Node returned an invalid RPC error.\n";
                return std::nullopt;
            }
            error_output << "RPC error ["
                         << response["error"]["code"].get_ref<const std::string&>()
                         << "]: "
                         << response["error"]["message"].get_ref<const std::string&>()
                         << '\n';
            return std::nullopt;
        }
        if (!response.contains("result")) {
            error_output << "Node returned an RPC response without a result.\n";
            return std::nullopt;
        }
        return std::move(response["result"]);
    } catch (const std::exception& error) {
        error_output << "Could not call local RPC: " << error.what() << '\n';
        return std::nullopt;
    }
}

int execute_rpc(
    const std::string_view command,
    const CommandOptions& options,
    std::ostream& output,
    std::ostream& error_output
) {
    const bool submit = command == "submit";
    if (!validate_options(
            options,
            submit
                ? std::initializer_list<std::string_view>{"--config", "--transaction"}
                : std::initializer_list<std::string_view>{"--config"},
            error_output
        )) {
        return usage_error;
    }
    const std::optional<std::string_view> config_path =
        required_option(options, "--config", error_output);
    if (!config_path.has_value()) {
        return usage_error;
    }

    config::ApplicationConfigResult loaded = config::load_application_config(
        std::filesystem::path{std::string{*config_path}}
    );
    if (!loaded.has_value()) {
        error_output << "Could not load application configuration: "
                     << config::application_config_error_message(loaded.error()) << '\n';
        return runtime_error;
    }
    const config::ApplicationConfig& application = loaded.value();
    if (!application.rpc.has_value()) {
        error_output << "Configuration has no local RPC settings.\n";
        return runtime_error;
    }
    rpc::TokenResult token = rpc::load_token(application.rpc->token_file);
    if (!token.has_value()) {
        error_output << "Could not load the RPC token: "
                     << rpc::token_error_message(token.error()) << '\n';
        return runtime_error;
    }

    std::string method;
    nlohmann::json params;
    if (command == "health" || command == "status" || command == "dump" ||
        command == "ping") {
        method = std::string{command};
    } else if (command == "start-mining") {
        method = "start_mining";
    } else if (command == "stop") {
        method = "shutdown";
    } else if (submit) {
        const std::optional<std::string_view> transaction_path =
            required_option(options, "--transaction", error_output);
        if (!transaction_path.has_value()) {
            return usage_error;
        }
        transaction::TransactionResult transaction = transaction::load_transaction(
            std::filesystem::path{std::string{*transaction_path}}
        );
        if (!transaction.has_value()) {
            error_output << "Could not read transaction: "
                         << transaction::transaction_error_message(transaction.error())
                         << '\n';
            return runtime_error;
        }
        method = "submit_signed_transaction";
        params = {
            {"transaction_hex", crypto::to_upper_hex(
                transaction.value().serialize()
            )},
        };
    } else {
        error_output << "Unknown RPC command: " << command << "\n\n";
        print_rpc_help(error_output);
        return usage_error;
    }

    const std::optional<nlohmann::json> result = call_rpc(
        *application.rpc,
        token.value(),
        method,
        std::move(params),
        error_output
    );
    if (!result.has_value()) {
        return runtime_error;
    }
    if (command == "health") {
        if (!result->is_object() || !result->contains("ready") ||
            !(*result)["ready"].is_boolean()) {
            error_output << "Node returned an invalid health result.\n";
            return runtime_error;
        }
        output << "RPC health: "
               << (result->value("ready", false) ? "ready" : "not ready") << '\n';
    } else if (command == "stop") {
        output << "Node shutdown requested.\n";
    } else if (command == "start-mining") {
        output << "Mining start requested.\n";
    } else if (command == "ping") {
        if (!result->is_object() || !result->contains("nonce") ||
            !(*result)["nonce"].is_number_unsigned()) {
            error_output << "Node returned an invalid ping result.\n";
            return runtime_error;
        }
        output << "Ping nonce: " << (*result)["nonce"].get<std::uint64_t>() << '\n';
    } else if (command == "submit") {
        if (!result->is_object() || !result->contains("transaction_id") ||
            !(*result)["transaction_id"].is_string()) {
            error_output << "Node returned an invalid transaction result.\n";
            return runtime_error;
        }
        output << "Transaction submitted: "
               << (*result)["transaction_id"].get_ref<const std::string&>() << '\n';
    } else {
        output << result->dump(2) << '\n';
    }
    return success;
}

}  // namespace

void print_rpc_help(std::ostream& output) {
    output << "RPC commands:\n"
           << "  bbc rpc health --config <path>\n"
           << "  bbc rpc status --config <path>\n"
           << "  bbc rpc dump --config <path>\n"
           << "  bbc rpc ping --config <path>\n"
           << "  bbc rpc start-mining --config <path>\n"
           << "  bbc rpc submit --config <path> --transaction <path>\n"
           << "  bbc rpc stop --config <path>\n";
}

int run_rpc_command(
    const std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    if (arguments.empty() || arguments.front() == "help") {
        print_rpc_help(output);
        return success;
    }
    constexpr std::array commands{
        std::string_view{"health"},
        std::string_view{"status"},
        std::string_view{"dump"},
        std::string_view{"ping"},
        std::string_view{"start-mining"},
        std::string_view{"submit"},
        std::string_view{"stop"},
    };
    if (std::ranges::find(commands, arguments.front()) == commands.end()) {
        error_output << "Unknown RPC command: " << arguments.front() << "\n\n";
        print_rpc_help(error_output);
        return usage_error;
    }
    const std::optional<CommandOptions> options =
        parse_options(arguments.subspan(1), error_output);
    if (!options.has_value()) {
        return usage_error;
    }
    return execute_rpc(arguments.front(), *options, output, error_output);
}

}  // namespace bbc::app::detail
