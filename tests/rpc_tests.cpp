#include "bbc/app/application.hpp"
#include "bbc/core/network.hpp"
#include "bbc/rpc/token.hpp"
#include "bbc/transaction/transaction.hpp"
#include "bbc/wallet/wallet.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace {

class TemporaryRpcDirectory final {
public:
    TemporaryRpcDirectory()
        : path_(std::filesystem::temp_directory_path() /
            ("bbc-rpc-test-" + std::to_string(counter_.fetch_add(1)))) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_);
    }

    TemporaryRpcDirectory(const TemporaryRpcDirectory&) = delete;
    TemporaryRpcDirectory& operator=(const TemporaryRpcDirectory&) = delete;

    ~TemporaryRpcDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    inline static std::atomic_uint64_t counter_{0};
    std::filesystem::path path_;
};

std::uint16_t available_loopback_port() {
    asio::io_context context;
    asio::ip::tcp::acceptor acceptor{
        context,
        {asio::ip::make_address_v4("127.0.0.1"), 0},
    };
    return acceptor.local_endpoint().port();
}

int run_cli(
    const std::initializer_list<std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
) {
    return bbc::app::run(
        std::span<const std::string_view>{arguments.begin(), arguments.size()},
        output,
        error_output
    );
}

class RunningApplicationNode final {
public:
    explicit RunningApplicationNode(std::filesystem::path config_path)
        : config_path_(std::move(config_path)),
          thread_([this] {
              const std::string config = config_path_.string();
              const std::array<std::string_view, 4> arguments{
                  "node", "run", "--config", config
              };
              result_.store(bbc::app::run(arguments, output_, error_output_));
          }) {}

    RunningApplicationNode(const RunningApplicationNode&) = delete;
    RunningApplicationNode& operator=(const RunningApplicationNode&) = delete;

    ~RunningApplicationNode() {
        std::ostringstream output;
        std::ostringstream error_output;
        const std::string config = config_path_.string();
        const std::array<std::string_view, 4> arguments{
            "rpc", "stop", "--config", config
        };
        static_cast<void>(bbc::app::run(arguments, output, error_output));
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] int result() const noexcept {
        return result_.load();
    }

    [[nodiscard]] const std::string output() const {
        return output_.str();
    }

    [[nodiscard]] const std::string error_output() const {
        return error_output_.str();
    }

private:
    std::filesystem::path config_path_;
    std::ostringstream output_;
    std::ostringstream error_output_;
    std::atomic_int result_{-1};
    std::thread thread_;
};

bool wait_for_rpc(const std::filesystem::path& config_path) {
    const std::string config = config_path.string();
    const std::array<std::string_view, 4> arguments{
        "rpc", "health", "--config", config
    };
    for (int attempt = 0; attempt < 500; ++attempt) {
        std::ostringstream output;
        std::ostringstream error_output;
        if (bbc::app::run(arguments, output, error_output) == 0) {
            return output.str() == "RPC health: ready\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

nlohmann::json raw_rpc(
    const std::uint16_t port,
    const std::string_view token,
    const std::string_view method
) {
    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    socket.connect({asio::ip::make_address_v4("127.0.0.1"), port});
    const std::string request = nlohmann::json{
        {"id", 17},
        {"method", method},
        {"token", token},
    }.dump() + '\n';
    asio::write(socket, asio::buffer(request));
    asio::streambuf buffer(64 * 1024);
    asio::read_until(socket, buffer, '\n');
    std::istream input{&buffer};
    std::string line;
    std::getline(input, line);
    return nlohmann::json::parse(line);
}

bool wait_for_height(
    const std::filesystem::path& config_path,
    const std::uint64_t height
) {
    const std::string config = config_path.string();
    const std::array<std::string_view, 4> arguments{
        "rpc", "status", "--config", config
    };
    for (int attempt = 0; attempt < 500; ++attempt) {
        std::ostringstream output;
        std::ostringstream error_output;
        if (bbc::app::run(arguments, output, error_output) == 0) {
            const nlohmann::json status = nlohmann::json::parse(output.str());
            if (status["chain"]["height"].get<std::uint64_t>() >= height) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

}  // namespace

TEST_CASE("RPC tokens are generated once and validated", "[rpc]") {
    TemporaryRpcDirectory directory;
    const std::filesystem::path token_path = directory.path() / "rpc.token";

    bbc::rpc::TokenResult created = bbc::rpc::load_or_create_token(token_path);
    REQUIRE(created.has_value());
    CHECK(created.value().size() == 64);
    CHECK(std::filesystem::is_regular_file(token_path));

    bbc::rpc::TokenResult reopened = bbc::rpc::load_or_create_token(token_path);
    REQUIRE(reopened.has_value());
    CHECK(reopened.value() == created.value());

    {
        std::ofstream invalid(token_path, std::ios::binary | std::ios::trunc);
        invalid << "not-a-token\n";
    }
    const bbc::rpc::TokenResult rejected = bbc::rpc::load_token(token_path);
    CHECK_FALSE(rejected.has_value());
    CHECK(rejected.error() == bbc::rpc::TokenError::invalid_token);
}

TEST_CASE("persistent node rejects non-loopback P2P before creating secrets", "[rpc][node]") {
    TemporaryRpcDirectory directory;
    const std::filesystem::path config_path = directory.path() / "bbc.json";
    const nlohmann::json config{
        {"schema_version", 1},
        {"name", "public-node"},
        {"network", "development"},
        {"roles", {"wallet", "full_node"}},
        {"wallet", {{"file", "wallet.dat"}}},
        {"data_directory", "data"},
        {"full_node", {
            {"listen", {{"host", "0.0.0.0"}, {"port", 7333}}},
            {"peers", nlohmann::json::array()},
        }},
        {"rpc", {
            {"listen", {{"host", "127.0.0.1"}, {"port", 7334}}},
            {"token_file", "rpc.token"},
        }},
    };
    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        output << config.dump(2) << '\n';
    }
    const std::string config_file = config_path.string();
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(run_cli(
        {"node", "run", "--config", config_file},
        output,
        error_output
    ) == 1);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("only a 127.0.0.1 P2P listener") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(directory.path() / "rpc.token"));
    CHECK_FALSE(std::filesystem::exists(directory.path() / "data"));
}

TEST_CASE("persistent node RPC mines and accepts a signed transaction", "[rpc][node]") {
    TemporaryRpcDirectory directory;
    const std::filesystem::path config_path = directory.path() / "bbc.json";
    const std::filesystem::path transaction_path = directory.path() / "payment.bbctx";
    const std::uint16_t p2p_port = available_loopback_port();
    std::uint16_t rpc_port = available_loopback_port();
    while (rpc_port == p2p_port) {
        rpc_port = available_loopback_port();
    }

    bbc::wallet::Wallet miner_wallet = bbc::wallet::Wallet::create();
    const bbc::wallet::Wallet recipient_wallet = bbc::wallet::Wallet::create();
    const nlohmann::json config{
        {"schema_version", 1},
        {"name", "persistent-node"},
        {"network", "regtest"},
        {"roles", {"wallet", "full_node", "miner"}},
        {"wallet", {{"file", "wallet.dat"}}},
        {"data_directory", "data"},
        {"full_node", {
            {"listen", {{"host", "127.0.0.1"}, {"port", p2p_port}}},
            {"peers", nlohmann::json::array()},
        }},
        {"miner", {
            {"reward_address", std::string{miner_wallet.address().value()}},
        }},
        {"rpc", {
            {"listen", {{"host", "127.0.0.1"}, {"port", rpc_port}}},
            {"token_file", "rpc.token"},
        }},
    };
    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        output << config.dump(2) << '\n';
    }

    RunningApplicationNode node{config_path};
    REQUIRE(wait_for_rpc(config_path));

    const nlohmann::json unauthorized = raw_rpc(rpc_port, std::string(64, '0'), "status");
    CHECK_FALSE(unauthorized["ok"].get<bool>());
    CHECK(unauthorized["error"]["code"] == "unauthorized");
    const bbc::rpc::TokenResult token = bbc::rpc::load_token(
        directory.path() / "rpc.token"
    );
    REQUIRE(token.has_value());
    const nlohmann::json scenario_only = raw_rpc(
        rpc_port,
        token.value(),
        "begin_mining"
    );
    CHECK_FALSE(scenario_only["ok"].get<bool>());
    CHECK(scenario_only["error"]["code"] == "unknown_method");

    std::ostringstream mining_output;
    std::ostringstream mining_error;
    const std::string config_file = config_path.string();
    CHECK(run_cli(
        {"rpc", "start-mining", "--config", config_file},
        mining_output,
        mining_error
    ) == 0);
    CHECK(mining_output.str() == "Mining start requested.\n");
    CHECK(mining_error.str().empty());
    REQUIRE(wait_for_height(config_path, 1));

    bbc::transaction::TransactionResult payment = bbc::transaction::sign_transaction(
        miner_wallet,
        {
            recipient_wallet.address(),
            100,
            1,
            0,
            bbc::core::network_parameters(
                bbc::core::NetworkProfile::regtest
            ).chain_id,
        }
    );
    REQUIRE(payment.has_value());
    REQUIRE(
        bbc::transaction::save_transaction(payment.value(), transaction_path) ==
        bbc::transaction::TransactionError::none
    );

    std::ostringstream submit_output;
    std::ostringstream submit_error;
    const std::string transaction_file = transaction_path.string();
    CHECK(run_cli(
        {"rpc", "submit", "--config", config_file, "--transaction", transaction_file},
        submit_output,
        submit_error
    ) == 0);
    CHECK(submit_output.str().starts_with("Transaction submitted: "));
    CHECK(submit_error.str().empty());

    std::ostringstream status_output;
    std::ostringstream status_error;
    CHECK(run_cli(
        {"rpc", "status", "--config", config_file},
        status_output,
        status_error
    ) == 0);
    const nlohmann::json status = nlohmann::json::parse(status_output.str());
    CHECK(status["chain"]["height"] == 1);
    CHECK(status["chain"]["mempool_size"] == 1);
    CHECK(status["mining"]["state"] == "accepted");
    CHECK(
        status["mining_reward_address"].get<std::string>() ==
        std::string{miner_wallet.address().value()}
    );

    std::ostringstream stop_output;
    std::ostringstream stop_error;
    CHECK(run_cli(
        {"rpc", "stop", "--config", config_file},
        stop_output,
        stop_error
    ) == 0);
    CHECK(stop_output.str() == "Node shutdown requested.\n");
    CHECK(stop_error.str().empty());

    for (int attempt = 0; attempt < 500 && node.result() == -1; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    CHECK(node.result() == 0);
    CHECK(node.error_output().empty());
    CHECK(node.output().find("\"event\":\"ready\"") != std::string::npos);
    CHECK(node.output().find("\"event\":\"stopped\"") != std::string::npos);
    CHECK(node.output().find(token.value()) == std::string::npos);
}
