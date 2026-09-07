#include "bbc/app/application.hpp"
#include "bbc/chain/state.hpp"
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
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

std::vector<std::uint16_t> available_loopback_ports(const std::size_t count) {
    asio::io_context context;
    std::vector<std::unique_ptr<asio::ip::tcp::acceptor>> acceptors;
    std::vector<std::uint16_t> ports;
    acceptors.reserve(count);
    ports.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto acceptor = std::make_unique<asio::ip::tcp::acceptor>(
            context,
            asio::ip::tcp::endpoint{asio::ip::make_address_v4("127.0.0.1"), 0}
        );
        ports.push_back(acceptor->local_endpoint().port());
        acceptors.push_back(std::move(acceptor));
    }
    return ports;
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
    for (int attempt = 0; attempt < 1'000; ++attempt) {
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
    for (int attempt = 0; attempt < 1'000; ++attempt) {
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

std::optional<nlohmann::json> rpc_json(
    const std::filesystem::path& config_path,
    const std::string_view command
) {
    const std::string config = config_path.string();
    const std::array<std::string_view, 4> arguments{
        "rpc", command, "--config", config
    };
    std::ostringstream output;
    std::ostringstream error_output;
    if (bbc::app::run(arguments, output, error_output) != 0) {
        return std::nullopt;
    }
    return nlohmann::json::parse(output.str());
}

template <typename Predicate>
bool wait_for_status(
    const std::filesystem::path& config_path,
    Predicate predicate
) {
    for (int attempt = 0; attempt < 1'000; ++attempt) {
        const std::optional<nlohmann::json> status =
            rpc_json(config_path, "status");
        if (status.has_value() && predicate(*status)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

bool request_stop(const std::filesystem::path& config_path) {
    const std::string config = config_path.string();
    std::ostringstream output;
    std::ostringstream error_output;
    return run_cli(
        {"rpc", "stop", "--config", config},
        output,
        error_output
    ) == 0;
}

bool wait_for_exit(const RunningApplicationNode& node) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (node.result() != -1) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

std::optional<nlohmann::json> account_entry(
    const nlohmann::json& dump,
    const std::string_view address
) {
    if (!dump.contains("chain") || !dump["chain"].contains("accounts") ||
        !dump["chain"]["accounts"].is_array()) {
        return std::nullopt;
    }
    for (const nlohmann::json& account : dump["chain"]["accounts"]) {
        if (account.value("address", std::string{}) == address) {
            return std::optional<nlohmann::json>{account};
        }
    }
    return std::nullopt;
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

TEST_CASE("persistent node rejects unsafe P2P before creating secrets", "[rpc][node]") {
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
    CHECK(error_output.str().find("outside the configured full-node scope") !=
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
            {"scope", "internet"},
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
    CHECK(status["p2p"]["scope"] == "internet");
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

TEST_CASE("persistent miner can run continuously and stop cleanly", "[rpc][node][mining]") {
    TemporaryRpcDirectory directory;
    const std::filesystem::path config_path = directory.path() / "bbc.json";
    const std::vector<std::uint16_t> ports = available_loopback_ports(2);
    const bbc::wallet::Wallet miner_wallet = bbc::wallet::Wallet::create();
    const nlohmann::json config{
        {"schema_version", 1},
        {"name", "continuous-miner"},
        {"network", "regtest"},
        {"roles", {"wallet", "full_node", "miner"}},
        {"wallet", {{"file", "wallet.dat"}}},
        {"data_directory", "data"},
        {"full_node", {
            {"scope", "loopback"},
            {"listen", {{"host", "127.0.0.1"}, {"port", ports[0]}}},
            {"peers", nlohmann::json::array()},
        }},
        {"miner", {
            {"reward_address", std::string{miner_wallet.address().value()}},
            {"auto_start", true},
        }},
        {"rpc", {
            {"listen", {{"host", "127.0.0.1"}, {"port", ports[1]}}},
            {"token_file", "rpc.token"},
        }},
    };
    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        output << config.dump(2) << '\n';
    }

    RunningApplicationNode node{config_path};
    REQUIRE(wait_for_rpc(config_path));
    REQUIRE(wait_for_height(config_path, 2));

    const std::string config_file = config_path.string();
    std::ostringstream stop_mining_output;
    std::ostringstream stop_mining_error;
    REQUIRE(run_cli(
        {"rpc", "stop-mining", "--config", config_file},
        stop_mining_output,
        stop_mining_error
    ) == 0);
    CHECK(stop_mining_output.str() == "Mining stop requested.\n");
    CHECK(stop_mining_error.str().empty());

    const std::optional<nlohmann::json> stopped = rpc_json(config_path, "status");
    REQUIRE(stopped.has_value());
    CHECK((*stopped)["mining"]["auto_start"]);
    CHECK_FALSE((*stopped)["mining"]["continuous"]);
    CHECK_FALSE((*stopped)["mining"]["active"]);
    CHECK((*stopped)["mining"]["state"] == "stopped");
    const std::uint64_t stopped_height =
        (*stopped)["chain"]["height"].get<std::uint64_t>();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    const std::optional<nlohmann::json> still_stopped =
        rpc_json(config_path, "status");
    REQUIRE(still_stopped.has_value());
    CHECK((*still_stopped)["chain"]["height"] == stopped_height);

    std::ostringstream restart_output;
    std::ostringstream restart_error;
    REQUIRE(run_cli(
        {"rpc", "start-continuous-mining", "--config", config_file},
        restart_output,
        restart_error
    ) == 0);
    CHECK(restart_output.str() == "Continuous mining start requested.\n");
    CHECK(restart_error.str().empty());
    REQUIRE(wait_for_height(config_path, stopped_height + 1));

    REQUIRE(run_cli(
        {"rpc", "stop-mining", "--config", config_file},
        stop_mining_output,
        stop_mining_error
    ) == 0);
    REQUIRE(request_stop(config_path));
    REQUIRE(wait_for_exit(node));
    CHECK(node.result() == 0);
    CHECK(node.error_output().empty());
    CHECK(node.output().find("\"event\":\"continuous_mining_started\"") !=
          std::string::npos);
    CHECK(node.output().find("\"event\":\"mining_stopped\"") !=
          std::string::npos);
}

TEST_CASE("two persistent nodes confirm payment and recover after restart", "[rpc][node][network]") {
    TemporaryRpcDirectory directory;
    const std::filesystem::path node_a_config = directory.path() / "node-a.json";
    const std::filesystem::path node_b_config = directory.path() / "node-b.json";
    const std::filesystem::path transaction_path = directory.path() / "payment.bbctx";
    const std::vector<std::uint16_t> ports = available_loopback_ports(4);

    bbc::wallet::Wallet miner_wallet = bbc::wallet::Wallet::create();
    const bbc::wallet::Wallet recipient_wallet = bbc::wallet::Wallet::create();
    const nlohmann::json node_a{
        {"schema_version", 1},
        {"name", "node-a"},
        {"network", "regtest"},
        {"roles", {"wallet", "full_node", "miner"}},
        {"wallet", {{"file", "miner.wallet"}}},
        {"data_directory", "node-a-data"},
        {"full_node", {
            {"scope", "loopback"},
            {"listen", {{"host", "127.0.0.1"}, {"port", ports[0]}}},
            {"peers", {
                {{"host", "127.0.0.2"}, {"port", ports[2]}},
            }},
        }},
        {"miner", {
            {"reward_address", std::string{miner_wallet.address().value()}},
        }},
        {"rpc", {
            {"listen", {{"host", "127.0.0.1"}, {"port", ports[1]}}},
            {"token_file", "node-a.token"},
        }},
    };
    const nlohmann::json node_b{
        {"schema_version", 1},
        {"name", "node-b"},
        {"network", "regtest"},
        {"roles", {"wallet", "full_node"}},
        {"wallet", {{"file", "recipient.wallet"}}},
        {"data_directory", "node-b-data"},
        {"full_node", {
            {"scope", "loopback"},
            {"listen", {{"host", "127.0.0.2"}, {"port", ports[2]}}},
            {"peers", nlohmann::json::array()},
        }},
        {"rpc", {
            {"listen", {{"host", "127.0.0.1"}, {"port", ports[3]}}},
            {"token_file", "node-b.token"},
        }},
    };
    {
        std::ofstream output(node_a_config, std::ios::binary | std::ios::trunc);
        output << node_a.dump(2) << '\n';
    }
    {
        std::ofstream output(node_b_config, std::ios::binary | std::ios::trunc);
        output << node_b.dump(2) << '\n';
    }

    constexpr std::uint64_t amount = 1'000'000'000;
    constexpr std::uint64_t fee = 1'000;
    std::string node_a_token;
    std::string node_b_token;
    {
        RunningApplicationNode running_a{node_a_config};
        REQUIRE(wait_for_rpc(node_a_config));

        {
            RunningApplicationNode running_b{node_b_config};
            REQUIRE(wait_for_rpc(node_b_config));
            const std::optional<nlohmann::json> initial_status_a =
                rpc_json(node_a_config, "status");
            REQUIRE(initial_status_a.has_value());
            CHECK((*initial_status_a)["p2p"]["scope"] == "loopback");
            REQUIRE(wait_for_status(node_a_config, [](const nlohmann::json& status) {
                return status["p2p"]["handshake_complete_count"] >= 1;
            }));
            REQUIRE(wait_for_status(node_b_config, [](const nlohmann::json& status) {
                return status["p2p"]["handshake_complete_count"] >= 1;
            }));

            std::ostringstream mining_output;
            std::ostringstream mining_error;
            const std::string config = node_a_config.string();
            REQUIRE(run_cli(
                {"rpc", "start-mining", "--config", config},
                mining_output,
                mining_error
            ) == 0);
            REQUIRE(wait_for_height(node_a_config, 1));
            REQUIRE(wait_for_height(node_b_config, 1));

            bbc::transaction::TransactionResult payment =
                bbc::transaction::sign_transaction(
                    miner_wallet,
                    {
                        recipient_wallet.address(),
                        amount,
                        fee,
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
            const std::string transaction = transaction_path.string();
            std::ostringstream submit_output;
            std::ostringstream submit_error;
            REQUIRE(run_cli(
                {"rpc", "submit", "--config", config, "--transaction", transaction},
                submit_output,
                submit_error
            ) == 0);
            REQUIRE(wait_for_status(node_a_config, [](const nlohmann::json& status) {
                return status["chain"]["mempool_size"] == 1;
            }));
            REQUIRE(wait_for_status(node_b_config, [](const nlohmann::json& status) {
                return status["chain"]["mempool_size"] == 1;
            }));

            const bbc::rpc::TokenResult token_a = bbc::rpc::load_token(
                directory.path() / "node-a.token"
            );
            const bbc::rpc::TokenResult token_b = bbc::rpc::load_token(
                directory.path() / "node-b.token"
            );
            REQUIRE(token_a.has_value());
            REQUIRE(token_b.has_value());
            node_a_token = token_a.value();
            node_b_token = token_b.value();

            REQUIRE(request_stop(node_b_config));
            REQUIRE(wait_for_exit(running_b));
            CHECK(running_b.result() == 0);
        }

        const std::string config = node_a_config.string();
        std::ostringstream mining_output;
        std::ostringstream mining_error;
        REQUIRE(run_cli(
            {"rpc", "start-mining", "--config", config},
            mining_output,
            mining_error
        ) == 0);
        REQUIRE(wait_for_height(node_a_config, 2));

        {
            RunningApplicationNode restarted_b{node_b_config};
            REQUIRE(wait_for_rpc(node_b_config));
            REQUIRE(wait_for_height(node_b_config, 2));
            REQUIRE(wait_for_status(node_b_config, [](const nlohmann::json& status) {
                return status["chain"]["mempool_size"] == 0 &&
                    status["sync"]["state"] == "complete";
            }));

            const std::optional<nlohmann::json> dump_a =
                rpc_json(node_a_config, "dump");
            const std::optional<nlohmann::json> dump_b =
                rpc_json(node_b_config, "dump");
            REQUIRE(dump_a.has_value());
            REQUIRE(dump_b.has_value());
            CHECK((*dump_a)["chain"]["tip"] == (*dump_b)["chain"]["tip"]);
            CHECK((*dump_a)["chain"]["height"] == 2);
            CHECK((*dump_b)["chain"]["height"] == 2);

            const std::optional<nlohmann::json> miner_a = account_entry(
                *dump_a,
                miner_wallet.address().value()
            );
            const std::optional<nlohmann::json> miner_b = account_entry(
                *dump_b,
                miner_wallet.address().value()
            );
            const std::optional<nlohmann::json> recipient_a = account_entry(
                *dump_a,
                recipient_wallet.address().value()
            );
            const std::optional<nlohmann::json> recipient_b = account_entry(
                *dump_b,
                recipient_wallet.address().value()
            );
            REQUIRE(miner_a.has_value());
            REQUIRE(miner_b.has_value());
            REQUIRE(recipient_a.has_value());
            REQUIRE(recipient_b.has_value());
            const std::uint64_t miner_balance =
                bbc::chain::block_subsidy * 2 - amount;
            CHECK((*miner_a)["balance"] == miner_balance);
            CHECK((*miner_b)["balance"] == miner_balance);
            CHECK((*miner_a)["next_nonce"] == 1);
            CHECK((*miner_b)["next_nonce"] == 1);
            CHECK((*recipient_a)["balance"] == amount);
            CHECK((*recipient_b)["balance"] == amount);
            CHECK((*recipient_a)["next_nonce"] == 0);
            CHECK((*recipient_b)["next_nonce"] == 0);

            REQUIRE(request_stop(node_b_config));
            REQUIRE(wait_for_exit(restarted_b));
            CHECK(restarted_b.result() == 0);
        }

        REQUIRE(request_stop(node_a_config));
        REQUIRE(wait_for_exit(running_a));
        CHECK(running_a.result() == 0);
    }

    {
        RunningApplicationNode restarted_a{node_a_config};
        RunningApplicationNode restarted_b{node_b_config};
        REQUIRE(wait_for_rpc(node_a_config));
        REQUIRE(wait_for_rpc(node_b_config));
        REQUIRE(wait_for_height(node_a_config, 2));
        REQUIRE(wait_for_height(node_b_config, 2));

        const bbc::rpc::TokenResult token_a = bbc::rpc::load_token(
            directory.path() / "node-a.token"
        );
        const bbc::rpc::TokenResult token_b = bbc::rpc::load_token(
            directory.path() / "node-b.token"
        );
        REQUIRE(token_a.has_value());
        REQUIRE(token_b.has_value());
        CHECK(token_a.value() == node_a_token);
        CHECK(token_b.value() == node_b_token);

        REQUIRE(request_stop(node_b_config));
        REQUIRE(request_stop(node_a_config));
        REQUIRE(wait_for_exit(restarted_b));
        REQUIRE(wait_for_exit(restarted_a));
        CHECK(restarted_a.result() == 0);
        CHECK(restarted_b.result() == 0);
    }
}
