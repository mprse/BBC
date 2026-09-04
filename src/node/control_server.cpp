#include "bbc/node/control_server.hpp"

#include "bbc/core/version.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/network/peer_network.hpp"
#include "bbc/network/protocol.hpp"
#include "bbc/storage/chain_store.hpp"
#include "bbc/storage/mempool_store.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bbc::node {
namespace {

constexpr std::size_t maximum_control_request_size = 64 * 1024;

class EventEmitter final {
public:
    EventEmitter(std::string actor, std::ostream& output)
        : actor_(std::move(actor)), output_(output) {}

    void emit(const std::string_view event, nlohmann::json details) {
        std::lock_guard lock{mutex_};
        nlohmann::json message{
            {"actor", actor_},
            {"sequence", sequence_++},
            {"event", event},
            {"details", std::move(details)},
        };
        output_ << message.dump() << '\n';
        output_.flush();
    }

private:
    std::string actor_;
    std::ostream& output_;
    std::mutex mutex_;
    std::uint64_t sequence_ = 0;
};

class NodeRuntime final {
public:
    explicit NodeRuntime(NodeConfig config) : config_(std::move(config)) {}

    [[nodiscard]] bool initialize(std::ostream& error_output) {
        if (config_.has_role(ActorRole::wallet)) {
            wallet_.emplace(wallet::Wallet::create());
        }
        if (!config_.has_role(ActorRole::full_node)) {
            return true;
        }

        std::error_code path_error;
        const bool chain_exists = std::filesystem::is_regular_file(
            config_.data_directory / "chain" / "blocks.dat",
            path_error
        );
        if (path_error && path_error != std::errc::no_such_file_or_directory) {
            error_output << "Could not inspect actor data directory.\n";
            return false;
        }
        storage::ChainStoreResult opened = chain_exists
            ? storage::ChainStore::open(config_.data_directory)
            : storage::ChainStore::initialize(config_.data_directory);
        if (!opened.has_value()) {
            error_output << "Could not initialize actor chain store: "
                         << storage::chain_store_error_message(opened.error()) << '\n';
            return false;
        }
        chain_store_.emplace(std::move(opened).value());
        storage::MempoolStoreResult mempool = storage::MempoolStore::open(
            config_.data_directory,
            chain_store_->blockchain().state()
        );
        if (!mempool.has_value()) {
            error_output << "Could not initialize actor mempool: "
                         << storage::mempool_store_error_message(mempool.error()) << '\n';
            return false;
        }
        return true;
    }

    [[nodiscard]] const NodeConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool start_network(
        EventEmitter& events,
        std::ostream& error_output
    ) {
        if (!chain_store_.has_value()) {
            return true;
        }
        const chain::Blockchain& blockchain = chain_store_->blockchain();
        std::uint64_t services = network::full_node_service;
        if (config_.has_role(ActorRole::miner)) {
            services |= network::mining_service;
        }
        network_.emplace(
            network::PeerNetworkConfig{
                config_.p2p_port,
                blockchain.tip().chain_id(),
                services,
                blockchain.tip().height(),
                blockchain.tip().id(),
                blockchain.blocks().front().id(),
            },
            [&events](const network::PeerEvent& event) {
                events.emit(
                    event.type,
                    {
                        {"peer_id", event.peer_id},
                        {"endpoint", event.endpoint},
                        {"detail", event.detail},
                    }
                );
            }
        );
        std::string error;
        if (!network_->start(error)) {
            error_output << "Could not start P2P listener: " << error << '\n';
            network_.reset();
            return false;
        }
        return true;
    }

    void stop_network() noexcept {
        if (network_.has_value()) {
            network_->stop();
        }
    }

    [[nodiscard]] bool connect_peer(
        const std::string_view host,
        const std::uint16_t port,
        std::string& error
    ) {
        if (!network_.has_value()) {
            error = "Actor does not provide a full-node P2P service.";
            return false;
        }
        return network_->connect(host, port, error);
    }

    [[nodiscard]] std::optional<std::uint64_t> ping_peers() {
        if (!network_.has_value()) {
            return std::nullopt;
        }
        return network_->ping_all();
    }

    [[nodiscard]] nlohmann::json status(const bool include_accounts) const {
        nlohmann::json result{
            {"name", config_.name},
            {"roles", role_names()},
            {"software_version", std::string{core::version()}},
            {"ready", true},
        };
        if (wallet_.has_value()) {
            result["wallet_address"] = std::string{wallet_->address().value()};
        }
        if (chain_store_.has_value()) {
            const chain::Blockchain& blockchain = chain_store_->blockchain();
            storage::MempoolStoreResult mempool = storage::MempoolStore::open(
                config_.data_directory,
                blockchain.state()
            );
            result["chain"] = {
                {"height", blockchain.tip().height()},
                {"tip", crypto::to_upper_hex(blockchain.tip().id())},
                {"block_count", blockchain.block_count()},
                {"account_count", blockchain.state().account_count()},
            };
            result["chain"]["mempool_size"] = mempool.has_value()
                ? nlohmann::json(mempool.value().mempool().size())
                : nlohmann::json(nullptr);
            if (include_accounts) {
                nlohmann::json accounts = nlohmann::json::array();
                for (const auto& [address_hash, account] : blockchain.state().accounts()) {
                    accounts.push_back({
                        {"address", std::string{
                            wallet::Address::from_hash(address_hash).value()
                        }},
                        {"balance", account.balance},
                        {"next_nonce", account.next_nonce},
                    });
                }
                result["chain"]["accounts"] = std::move(accounts);
            }
        }
        if (network_.has_value()) {
            nlohmann::json peers = nlohmann::json::array();
            std::size_t completed = 0;
            for (const network::PeerStatus& peer : network_->peers()) {
                if (peer.handshake_complete) {
                    ++completed;
                }
                nlohmann::json encoded{
                    {"id", peer.id},
                    {"direction", network::peer_direction_name(peer.direction)},
                    {"endpoint", peer.endpoint},
                    {"handshake_complete", peer.handshake_complete},
                    {"protocol_version", peer.protocol_version},
                    {"services", peer.services},
                    {"remote_height", peer.remote_height},
                    {"remote_tip", crypto::to_upper_hex(peer.remote_tip)},
                };
                encoded["last_ping_nonce"] = peer.last_ping_nonce.has_value()
                    ? nlohmann::json(*peer.last_ping_nonce)
                    : nlohmann::json(nullptr);
                encoded["last_pong_nonce"] = peer.last_pong_nonce.has_value()
                    ? nlohmann::json(*peer.last_pong_nonce)
                    : nlohmann::json(nullptr);
                peers.push_back(std::move(encoded));
            }
            result["p2p"] = {
                {"host", "127.0.0.1"},
                {"port", network_->listen_port()},
                {"peer_count", peers.size()},
                {"handshake_complete_count", completed},
                {"peers", std::move(peers)},
            };
        }
        return result;
    }

private:
    [[nodiscard]] std::vector<std::string> role_names() const {
        std::vector<std::string> names;
        names.reserve(config_.roles.size());
        for (const ActorRole role : config_.roles) {
            names.emplace_back(actor_role_name(role));
        }
        return names;
    }

    NodeConfig config_;
    std::optional<wallet::Wallet> wallet_;
    std::optional<storage::ChainStore> chain_store_;
    std::optional<network::PeerNetwork> network_;
};

nlohmann::json error_response(
    const nlohmann::json& id,
    const std::string_view code,
    const std::string_view message
) {
    return {
        {"id", id},
        {"ok", false},
        {"error", {{"code", code}, {"message", message}}},
    };
}

nlohmann::json handle_request(
    const nlohmann::json& request,
    NodeRuntime& runtime,
    bool& stopping
) {
    const nlohmann::json id = request.is_object() && request.contains("id")
        ? request["id"]
        : nlohmann::json{nullptr};
    if (!request.is_object() || !request.contains("id") ||
        !request.contains("method") || !request["method"].is_string() ||
        !request.contains("token") || !request["token"].is_string()) {
        return error_response(id, "invalid_request", "Invalid control request.");
    }
    if (request["token"].get_ref<const std::string&>() !=
        runtime.config().control_token) {
        return error_response(id, "unauthorized", "Invalid control token.");
    }

    const std::string& method = request["method"].get_ref<const std::string&>();
    if (method == "health") {
        return {{"id", id}, {"ok", true}, {"result", {{"ready", true}}}};
    }
    if (method == "status") {
        return {{"id", id}, {"ok", true}, {"result", runtime.status(false)}};
    }
    if (method == "dump") {
        return {{"id", id}, {"ok", true}, {"result", runtime.status(true)}};
    }
    if (method == "connect_peer") {
        if (!request.contains("params") || !request["params"].is_object()) {
            return error_response(id, "invalid_params", "Missing peer endpoint.");
        }
        const nlohmann::json& params = request["params"];
        if (!params.contains("host") || !params["host"].is_string() ||
            !params.contains("port") || !params["port"].is_number_unsigned()) {
            return error_response(id, "invalid_params", "Invalid peer endpoint.");
        }
        const std::uint64_t port = params["port"].get<std::uint64_t>();
        if (port == 0 || port > 65'535) {
            return error_response(id, "invalid_params", "Invalid peer port.");
        }
        std::string error;
        if (!runtime.connect_peer(
                params["host"].get_ref<const std::string&>(),
                static_cast<std::uint16_t>(port),
                error
            )) {
            return error_response(id, "connect_rejected", error);
        }
        return {
            {"id", id},
            {"ok", true},
            {"result", {{"connecting", true}}},
        };
    }
    if (method == "ping") {
        const std::optional<std::uint64_t> nonce = runtime.ping_peers();
        if (!nonce.has_value()) {
            return error_response(id, "not_full_node", "Actor has no P2P service.");
        }
        return {
            {"id", id},
            {"ok", true},
            {"result", {{"nonce", *nonce}}},
        };
    }
    if (method == "shutdown") {
        stopping = true;
        return {{"id", id}, {"ok", true}, {"result", {{"stopping", true}}}};
    }
    return error_response(id, "unknown_method", "Unknown control method.");
}

void serve_connection(
    asio::ip::tcp::socket& socket,
    NodeRuntime& runtime,
    bool& stopping
) {
    asio::streambuf buffer(maximum_control_request_size);
    asio::error_code read_error;
    asio::read_until(socket, buffer, '\n', read_error);
    if (read_error) {
        return;
    }
    std::istream input(&buffer);
    std::string line;
    std::getline(input, line);

    nlohmann::json response;
    try {
        const nlohmann::json request = nlohmann::json::parse(line);
        response = handle_request(request, runtime, stopping);
    } catch (const nlohmann::json::exception&) {
        response = error_response(nullptr, "invalid_json", "Invalid control JSON.");
    }
    const std::string encoded = response.dump() + '\n';
    asio::error_code write_error;
    asio::write(socket, asio::buffer(encoded), write_error);
}

}  // namespace

int run_controlled_node(
    NodeConfig config,
    std::ostream& output,
    std::ostream& error_output
) {
    NodeRuntime runtime{std::move(config)};
    if (!runtime.initialize(error_output)) {
        return 1;
    }

    EventEmitter events{runtime.config().name, output};
    if (!runtime.start_network(events, error_output)) {
        return 1;
    }

    try {
        asio::io_context context;
        asio::ip::tcp::acceptor acceptor{
            context,
            asio::ip::tcp::endpoint{
                asio::ip::make_address_v4("127.0.0.1"),
                runtime.config().control_port,
            },
        };
        nlohmann::json ready_details{
            {"control_host", "127.0.0.1"},
            {"control_port", acceptor.local_endpoint().port()},
        };
        if (runtime.config().has_role(ActorRole::full_node)) {
            ready_details["p2p_host"] = "127.0.0.1";
            ready_details["p2p_port"] = runtime.status(false)["p2p"]["port"];
        }
        events.emit(
            "ready",
            std::move(ready_details)
        );

        bool stopping = false;
        while (!stopping) {
            asio::ip::tcp::socket socket{context};
            acceptor.accept(socket);
            serve_connection(socket, runtime, stopping);
        }
        runtime.stop_network();
        events.emit("stopped", {});
    } catch (const std::exception& error) {
        runtime.stop_network();
        error_output << "Node control server failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

}  // namespace bbc::node
