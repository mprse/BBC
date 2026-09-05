#include "bbc/app/application.hpp"
#include "bbc/config/application_config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view reward_address =
    "BBC_4400000000000000000000000000000000000000000000000000000000000000";

class TemporaryApplicationConfig final {
public:
    TemporaryApplicationConfig(
        const std::string_view name,
        const std::string_view contents
    ) : path_(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove(path_, error);
        std::ofstream output(path_, std::ios::binary);
        REQUIRE(output);
        output << contents;
        REQUIRE(output);
    }

    TemporaryApplicationConfig(const TemporaryApplicationConfig&) = delete;
    TemporaryApplicationConfig& operator=(const TemporaryApplicationConfig&) = delete;

    ~TemporaryApplicationConfig() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::string hybrid_config() {
    return std::string{R"({
        "schema_version": 1,
        "name": "home-node",
        "network": "development",
        "roles": ["wallet", "full_node", "miner"],
        "wallet": {"file": "wallet.dat"},
        "data_directory": "data",
        "full_node": {
            "listen": {"host": "127.0.0.1", "port": 7333},
            "peers": [
                {"host": "seed.example.org", "port": 7333},
                {"host": "192.168.1.20", "port": 8333}
            ]
        },
        "miner": {"reward_address": ")"} + std::string{reward_address} + R"("},
        "rpc": {
            "listen": {"host": "127.0.0.1", "port": 7334},
            "token_file": "rpc.token"
        }
    })";
}

}  // namespace

TEST_CASE("application configuration loads all persistent roles", "[config]") {
    TemporaryApplicationConfig file{
        "bbc-application-config-hybrid.json",
        hybrid_config(),
    };

    bbc::config::ApplicationConfigResult loaded =
        bbc::config::load_application_config(file.path());

    REQUIRE(loaded.has_value());
    const bbc::config::ApplicationConfig& config = loaded.value();
    CHECK(config.name == "home-node");
    CHECK(config.network_profile == bbc::core::NetworkProfile::development);
    CHECK(config.has_role(bbc::config::ApplicationRole::wallet));
    CHECK(config.has_role(bbc::config::ApplicationRole::full_node));
    CHECK(config.has_role(bbc::config::ApplicationRole::miner));
    CHECK(config.wallet_file == file.path().parent_path() / "wallet.dat");
    CHECK(config.data_directory == file.path().parent_path() / "data");
    REQUIRE(config.full_node.has_value());
    CHECK(config.full_node->listen.host == "127.0.0.1");
    CHECK(config.full_node->listen.port == 7333);
    REQUIRE(config.full_node->peers.size() == 2);
    CHECK(config.full_node->peers.front().host == "seed.example.org");
    REQUIRE(config.miner.has_value());
    CHECK(config.miner->reward_address.value() == reward_address);
    CHECK_FALSE(config.miner->source.has_value());
    REQUIRE(config.rpc.has_value());
    CHECK(config.rpc->listen.port == 7334);
    CHECK(config.rpc->token_file == file.path().parent_path() / "rpc.token");
}

TEST_CASE("wallet-only application configuration needs no service sections", "[config]") {
    TemporaryApplicationConfig file{
        "bbc-application-config-wallet.json",
        R"({
            "schema_version": 1,
            "name": "wallet-only",
            "network": "regtest",
            "roles": ["wallet"],
            "wallet": {"file": "wallet.dat"},
            "data_directory": "data"
        })",
    };

    const bbc::config::ApplicationConfigResult loaded =
        bbc::config::load_application_config(file.path());

    REQUIRE(loaded.has_value());
    CHECK(loaded.value().network_profile == bbc::core::NetworkProfile::regtest);
    CHECK_FALSE(loaded.value().full_node.has_value());
    CHECK_FALSE(loaded.value().miner.has_value());
    CHECK_FALSE(loaded.value().rpc.has_value());
}

TEST_CASE("mining-only application configuration requires a remote source", "[config]") {
    TemporaryApplicationConfig file{
        "bbc-application-config-miner.json",
        std::string{R"({
            "schema_version": 1,
            "name": "worker",
            "network": "development",
            "roles": ["wallet", "miner"],
            "wallet": {"file": "wallet.dat"},
            "data_directory": "data",
            "miner": {
                "reward_address": ")"} + std::string{reward_address} + R"(",
                "source": {"host": "node.example.org", "port": 7333}
            },
            "rpc": {
                "listen": {"host": "127.0.0.1", "port": 7334},
                "token_file": "rpc.token"
            }
        })",
    };

    const bbc::config::ApplicationConfigResult loaded =
        bbc::config::load_application_config(file.path());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded.value().miner.has_value());
    REQUIRE(loaded.value().miner->source.has_value());
    CHECK(loaded.value().miner->source->host == "node.example.org");
    CHECK(loaded.value().miner->source->port == 7333);
}

TEST_CASE("application configuration enforces role dependencies", "[config]") {
    SECTION("wallet is mandatory") {
        TemporaryApplicationConfig file{
            "bbc-application-config-no-wallet-role.json",
            R"({
                "schema_version": 1,
                "name": "invalid",
                "network": "development",
                "roles": ["full_node"],
                "wallet": {"file": "wallet.dat"},
                "data_directory": "data",
                "full_node": {
                    "listen": {"host": "127.0.0.1", "port": 7333}
                },
                "rpc": {
                    "listen": {"host": "127.0.0.1", "port": 7334},
                    "token_file": "rpc.token"
                }
            })",
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_roles);
    }

    SECTION("mining-only configuration needs a source") {
        TemporaryApplicationConfig file{
            "bbc-application-config-miner-source.json",
            std::string{R"({
                "schema_version": 1,
                "name": "invalid",
                "network": "development",
                "roles": ["wallet", "miner"],
                "wallet": {"file": "wallet.dat"},
                "data_directory": "data",
                "miner": {"reward_address": ")"} +
                std::string{reward_address} + R"("},
                "rpc": {
                    "listen": {"host": "127.0.0.1", "port": 7334},
                    "token_file": "rpc.token"
                }
            })",
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_miner);
    }

    SECTION("wallet-only configuration rejects RPC") {
        TemporaryApplicationConfig file{
            "bbc-application-config-wallet-rpc.json",
            R"({
                "schema_version": 1,
                "name": "invalid",
                "network": "development",
                "roles": ["wallet"],
                "wallet": {"file": "wallet.dat"},
                "data_directory": "data",
                "rpc": {
                    "listen": {"host": "127.0.0.1", "port": 7334},
                    "token_file": "rpc.token"
                }
            })",
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_rpc);
    }
}

TEST_CASE("application configuration rejects unsafe or ambiguous input", "[config]") {
    SECTION("unknown fields are rejected") {
        TemporaryApplicationConfig file{
            "bbc-application-config-unknown.json",
            R"({
                "schema_version": 1,
                "name": "wallet-only",
                "network": "development",
                "roles": ["wallet"],
                "wallet": {"file": "wallet.dat"},
                "data_directory": "data",
                "pasword": "secret"
            })",
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::unknown_field);
    }

    SECTION("RPC cannot listen outside loopback") {
        std::string encoded = hybrid_config();
        const std::size_t position = encoded.rfind("127.0.0.1");
        REQUIRE(position != std::string::npos);
        encoded.replace(position, std::string{"127.0.0.1"}.size(), "0.0.0.0");
        TemporaryApplicationConfig file{
            "bbc-application-config-public-rpc.json",
            encoded,
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_rpc);
    }

    SECTION("duplicate initial peers are rejected") {
        const std::string encoded = std::string{R"({
            "schema_version": 1,
            "name": "full-node",
            "network": "development",
            "roles": ["wallet", "full_node"],
            "wallet": {"file": "wallet.dat"},
            "data_directory": "data",
            "full_node": {
                "listen": {"host": "127.0.0.1", "port": 7333},
                "peers": [
                    {"host": "seed.example.org", "port": 7333},
                    {"host": "seed.example.org", "port": 7333}
                ]
            },
            "rpc": {
                "listen": {"host": "127.0.0.1", "port": 7334},
                "token_file": "rpc.token"
            }
        })"};
        TemporaryApplicationConfig file{
            "bbc-application-config-duplicate-peer.json",
            encoded,
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_full_node);
    }

    SECTION("malformed numeric IP addresses are rejected") {
        std::string encoded = hybrid_config();
        const std::string original = "192.168.1.20";
        const std::size_t position = encoded.find(original);
        REQUIRE(position != std::string::npos);
        encoded.replace(position, original.size(), "999.999.999.999");
        TemporaryApplicationConfig file{
            "bbc-application-config-invalid-ip.json",
            encoded,
        };
        const auto loaded = bbc::config::load_application_config(file.path());
        CHECK_FALSE(loaded.has_value());
        CHECK(loaded.error() == bbc::config::ApplicationConfigError::invalid_full_node);
    }
}

TEST_CASE("configuration CLI validates and shows normalized settings", "[config][application]") {
    TemporaryApplicationConfig file{
        "bbc-application-config-cli.json",
        hybrid_config(),
    };
    const std::string path = file.path().string();

    const std::array<std::string_view, 4> validate_arguments{
        "config", "validate", "--file", path
    };
    std::ostringstream validate_output;
    std::ostringstream validate_error;
    CHECK(bbc::app::run(validate_arguments, validate_output, validate_error) == 0);
    CHECK(validate_output.str().find("Configuration validation: success") !=
          std::string::npos);
    CHECK(validate_error.str().empty());

    const std::array<std::string_view, 4> show_arguments{
        "config", "show", "--file", path
    };
    std::ostringstream show_output;
    std::ostringstream show_error;
    CHECK(bbc::app::run(show_arguments, show_output, show_error) == 0);
    CHECK(show_output.str().find("Roles: wallet, full_node, miner") !=
          std::string::npos);
    CHECK(show_output.str().find("P2P listen: 127.0.0.1:7333") !=
          std::string::npos);
    CHECK(show_output.str().find("Mining source: local full node") !=
          std::string::npos);
    CHECK(show_output.str().find("RPC listen: 127.0.0.1:7334") !=
          std::string::npos);
    CHECK(show_error.str().empty());
}

TEST_CASE("configuration CLI treats a missing file option as usage error", "[config][application]") {
    constexpr std::array<std::string_view, 2> arguments{"config", "validate"};
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(bbc::app::run(arguments, output, error_output) == 2);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("Missing required option: --file") !=
          std::string::npos);
}

TEST_CASE("normal config cannot enter the scenario actor command", "[config][application]") {
    constexpr std::array<std::string_view, 4> arguments{
        "node", "run", "--config", "bbc.json"
    };
    std::ostringstream output;
    std::ostringstream error_output;

    CHECK(bbc::app::run(arguments, output, error_output) == 2);
    CHECK(output.str().empty());
    CHECK(error_output.str().find("Unknown option: --config") !=
          std::string::npos);
}
