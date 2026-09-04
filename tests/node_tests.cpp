#include "bbc/node/node_config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

class TemporaryNodeConfig final {
public:
    explicit TemporaryNodeConfig(
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

    TemporaryNodeConfig(const TemporaryNodeConfig&) = delete;
    TemporaryNodeConfig& operator=(const TemporaryNodeConfig&) = delete;

    ~TemporaryNodeConfig() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("node configuration loads actor roles and loopback control", "[node]") {
    TemporaryNodeConfig config{
        "bbc-stage7-node-config.json",
        R"({
            "schema_version": 1,
            "name": "node-a",
            "roles": ["wallet", "full_node"],
            "data_directory": "node-a-data",
            "p2p": {
                "host": "127.0.0.1",
                "port": 0
            },
            "control": {
                "host": "127.0.0.1",
                "port": 0,
                "token": "0123456789abcdef0123456789abcdef"
            }
        })"
    };

    bbc::node::NodeConfigResult loaded =
        bbc::node::load_node_config(config.path());

    REQUIRE(loaded.has_value());
    CHECK(loaded.value().name == "node-a");
    CHECK(loaded.value().control_port == 0);
    CHECK(loaded.value().p2p_port == 0);
    CHECK(loaded.value().has_role(bbc::node::ActorRole::wallet));
    CHECK(loaded.value().has_role(bbc::node::ActorRole::full_node));
    CHECK_FALSE(loaded.value().has_role(bbc::node::ActorRole::miner));
}

TEST_CASE("full-node configuration requires a loopback P2P endpoint", "[node]") {
    TemporaryNodeConfig config{
        "bbc-stage7-invalid-p2p-node-config.json",
        R"({
            "schema_version": 1,
            "name": "node-a",
            "roles": ["full_node"],
            "data_directory": "node-a-data",
            "p2p": {"host": "0.0.0.0", "port": 19001},
            "control": {
                "host": "127.0.0.1",
                "port": 20001,
                "token": "0123456789abcdef0123456789abcdef"
            }
        })"
    };

    const bbc::node::NodeConfigResult loaded =
        bbc::node::load_node_config(config.path());

    CHECK_FALSE(loaded.has_value());
    CHECK(loaded.error() == bbc::node::NodeConfigError::invalid_p2p_endpoint);
}

TEST_CASE("node configuration rejects non-loopback control endpoints", "[node]") {
    TemporaryNodeConfig config{
        "bbc-stage7-invalid-node-config.json",
        R"({
            "schema_version": 1,
            "name": "node-a",
            "roles": ["wallet"],
            "data_directory": "node-a-data",
            "control": {
                "host": "0.0.0.0",
                "port": 20001,
                "token": "0123456789abcdef0123456789abcdef"
            }
        })"
    };

    const bbc::node::NodeConfigResult loaded =
        bbc::node::load_node_config(config.path());

    CHECK_FALSE(loaded.has_value());
    CHECK(
        loaded.error() == bbc::node::NodeConfigError::invalid_control_endpoint
    );
}
