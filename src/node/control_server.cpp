#include "bbc/node/control_server.hpp"

#include "bbc/core/version.hpp"
#include "bbc/core/network.hpp"
#include "bbc/consensus/proof_of_work.hpp"
#include "bbc/chain/block.hpp"
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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
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

    ~NodeRuntime() {
        stop_network();
    }

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
        const std::uint32_t chain_id =
            core::network_parameters(config_.network_profile).chain_id;
        storage::ChainStoreResult opened = chain_exists
            ? storage::ChainStore::open(config_.data_directory, chain_id)
            : storage::ChainStore::initialize(config_.data_directory, chain_id);
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
        if (!config_.has_role(ActorRole::full_node) &&
            !config_.has_role(ActorRole::miner)) {
            return true;
        }
        const core::NetworkParameters& parameters =
            core::network_parameters(config_.network_profile);
        const chain::Block genesis = chain::genesis_block(parameters.chain_id);
        std::uint64_t services = config_.has_role(ActorRole::full_node)
            ? network::full_node_service
            : 0;
        if (config_.has_role(ActorRole::miner)) {
            services |= network::mining_service;
        }
        const std::uint64_t tip_height = chain_store_.has_value()
            ? chain_store_->blockchain().tip().height()
            : genesis.height();
        const crypto::Hash256 tip_id = chain_store_.has_value()
            ? chain_store_->blockchain().tip().id()
            : genesis.id();
        events_ = &events;
        network_.emplace(
            network::PeerNetworkConfig{
                config_.p2p_port,
                config_.has_role(ActorRole::full_node),
                parameters.chain_id,
                services,
                tip_height,
                tip_id,
                genesis.id(),
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
            },
            [this](
                const std::uint64_t peer_id,
                const network::MessageType type,
                const crypto::Bytes& payload
            ) { handle_peer_message(peer_id, type, payload); }
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
        mining_cancelled_.store(true);
        if (network_.has_value()) {
            network_->stop();
        }
        if (mining_thread_.joinable()) {
            mining_thread_.join();
        }
        if (network_.has_value()) {
            network_.reset();
        }
    }

    [[nodiscard]] bool connect_peer(
        const std::string_view host,
        const std::uint16_t port,
        std::string& error
    ) {
        if (!network_.has_value()) {
            error = "Actor does not provide a P2P service.";
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

    [[nodiscard]] bool start_mining(std::string& error) {
        if (!config_.has_role(ActorRole::miner) || !wallet_.has_value() ||
            !network_.has_value()) {
            error = "Actor is not a networked wallet miner.";
            return false;
        }
        if (mining_active_.load()) {
            error = "Mining is already active.";
            return false;
        }
        const auto peers = network_->peers();
        const auto source = std::ranges::find_if(peers, [](const auto& peer) {
            return peer.handshake_complete &&
                (peer.services & network::full_node_service) != 0;
        });
        if (source == peers.end()) {
            error = "Miner has no connected full node.";
            return false;
        }
        const std::uint64_t request_id = next_template_request_++;
        crypto::Bytes payload;
        append_u64(payload, request_id);
        const wallet::Address reward_address = wallet_->address();
        payload.insert(
            payload.end(),
            reward_address.hash().begin(),
            reward_address.hash().end()
        );
        {
            std::lock_guard lock{mining_mutex_};
            mining_source_peer_ = source->id;
            pending_template_request_ = request_id;
        }
        if (!network_->send(source->id, network::MessageType::template_request, payload)) {
            std::lock_guard lock{mining_mutex_};
            mining_source_peer_.reset();
            pending_template_request_.reset();
            error = "Could not send the block template request.";
            return false;
        }
        emit("mining_template_requested", {{"peer_id", source->id}});
        return true;
    }

    [[nodiscard]] nlohmann::json status(const bool include_accounts) const {
        nlohmann::json result{
            {"name", config_.name},
            {"roles", role_names()},
            {"software_version", std::string{core::version()}},
            {"ready", true},
            {"network", std::string{core::network_parameters(config_.network_profile).name}},
        };
        if (wallet_.has_value()) {
            result["wallet_address"] = std::string{wallet_->address().value()};
        }
        if (chain_store_.has_value()) {
            std::lock_guard lock{state_mutex_};
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
        if (config_.has_role(ActorRole::miner)) {
            std::lock_guard lock{mining_mutex_};
            result["mining"] = {
                {"active", mining_active_.load()},
                {"attempts", mining_attempts_.load()},
                {"state", mining_state_},
            };
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
    static void append_u64(crypto::Bytes& output, const std::uint64_t value) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
        }
    }

    static std::uint64_t read_u64(const crypto::ByteView input) {
        std::uint64_t value = 0;
        for (unsigned int index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
        }
        return value;
    }

    void emit(const std::string_view type, nlohmann::json details) const {
        if (events_ != nullptr) {
            events_->emit(type, std::move(details));
        }
    }

    void set_mining_state(const std::string_view state) {
        std::lock_guard lock{mining_mutex_};
        mining_state_ = state;
    }

    void send_block_result(
        const std::uint64_t peer_id,
        const crypto::Hash256& block_id,
        const bool accepted,
        const std::uint16_t reason
    ) {
        crypto::Bytes payload{block_id.begin(), block_id.end()};
        payload.push_back(accepted ? 1 : 0);
        payload.push_back(static_cast<crypto::Byte>(reason & 0xFFU));
        payload.push_back(static_cast<crypto::Byte>(reason >> 8U));
        static_cast<void>(network_->send(peer_id, network::MessageType::block_result, payload));
    }

    void handle_peer_message(
        const std::uint64_t peer_id,
        const network::MessageType type,
        const crypto::ByteView payload
    ) {
        if (type == network::MessageType::template_request) {
            handle_template_request(peer_id, payload);
        } else if (type == network::MessageType::block_template) {
            handle_block_template(peer_id, payload);
        } else if (type == network::MessageType::block_submit) {
            handle_block(peer_id, payload, true);
        } else if (type == network::MessageType::block) {
            handle_block(peer_id, payload, false);
        } else if (type == network::MessageType::block_result) {
            handle_block_result(payload);
        }
    }

    void handle_template_request(
        const std::uint64_t peer_id,
        const crypto::ByteView payload
    ) {
        if (!chain_store_.has_value() || payload.size() != 40) {
            return;
        }
        crypto::Hash256 reward_hash{};
        std::ranges::copy(payload.subspan(8), reward_hash.begin());
        const wallet::Address reward = wallet::Address::from_hash(reward_hash);
        chain::BlockResult candidate{chain::BlockError::invalid_genesis};
        {
            std::lock_guard lock{state_mutex_};
            const chain::Block& tip = chain_store_->blockchain().tip();
            candidate = chain::create_block({
                tip.height() + 1,
                tip.id(),
                reward,
                tip.timestamp() + 1,
                consensus::fixed_difficulty_target(tip.chain_id()),
                0,
                {},
                tip.chain_id(),
            });
        }
        if (!candidate.has_value()) {
            return;
        }
        crypto::Bytes response;
        append_u64(response, read_u64(payload));
        const crypto::Bytes encoded = candidate.value().serialize();
        response.insert(response.end(), encoded.begin(), encoded.end());
        static_cast<void>(network_->send(peer_id, network::MessageType::block_template, response));
        emit("block_template_sent", {{"peer_id", peer_id}, {"height", candidate.value().height()}});
    }

    void handle_block_template(
        const std::uint64_t peer_id,
        const crypto::ByteView payload
    ) {
        bool wrong_source_or_request = false;
        const std::uint64_t request_id = read_u64(payload);
        {
            std::lock_guard lock{mining_mutex_};
            wrong_source_or_request = !mining_source_peer_.has_value() ||
                peer_id != *mining_source_peer_ ||
                !pending_template_request_.has_value() ||
                request_id != *pending_template_request_;
        }
        if (!config_.has_role(ActorRole::miner) || payload.size() <= 8 ||
            wrong_source_or_request) {
            return;
        }
        chain::BlockResult decoded = chain::deserialize_block(payload.subspan(8));
        if (!decoded.has_value()) {
            emit("mining_template_rejected", {{"reason", "invalid_block"}});
            return;
        }
        const auto peers = network_->peers();
        const auto source = std::ranges::find_if(peers, [peer_id](const auto& peer) {
            return peer.id == peer_id && peer.handshake_complete;
        });
        const core::NetworkParameters& parameters =
            core::network_parameters(config_.network_profile);
        const wallet::Address own_address = wallet_->address();
        if (source == peers.end() || decoded.value().chain_id() != parameters.chain_id ||
            decoded.value().height() != source->remote_height + 1 ||
            decoded.value().previous_block_hash() != source->remote_tip ||
            decoded.value().difficulty_target() != parameters.difficulty_target ||
            !decoded.value().reward_recipient().has_value() ||
            *decoded.value().reward_recipient() != own_address) {
            emit("mining_template_rejected", {{"reason", "unexpected_template"}});
            return;
        }
        {
            std::lock_guard lock{mining_mutex_};
            pending_template_request_.reset();
            mining_parent_ = decoded.value().previous_block_hash();
            mining_height_ = decoded.value().height();
        }
        if (mining_thread_.joinable()) {
            mining_cancelled_.store(true);
            mining_thread_.join();
        }
        mining_cancelled_.store(false);
        mining_active_.store(true);
        mining_attempts_.store(0);
        set_mining_state("running");
        const auto height = decoded.value().height();
        emit("mining_started", {{"height", height}, {"batch_size", 10000}});
        mining_thread_ = std::thread{
            [this, peer_id, candidate = std::move(decoded).value()]() mutable {
                auto last_progress = std::chrono::steady_clock::now();
                while (!mining_cancelled_.load()) {
                    consensus::MiningResult result = consensus::mine_block(candidate, 10'000);
                    mining_attempts_.fetch_add(result.attempts());
                    if (result.has_value()) {
                        const chain::Block mined = std::move(result).value();
                        {
                            std::lock_guard lock{mining_mutex_};
                            found_block_id_ = mined.id();
                        }
                        mining_active_.store(false);
                        set_mining_state("submitted");
                        emit("block_found", {
                            {"block_id", crypto::to_upper_hex(mined.id())},
                            {"attempts", mining_attempts_.load()},
                        });
                        static_cast<void>(network_->send(
                            peer_id,
                            network::MessageType::block_submit,
                            mined.serialize()
                        ));
                        emit("block_submitted", {{"peer_id", peer_id}});
                        return;
                    }
                    if (!result.next_nonce().has_value()) {
                        mining_active_.store(false);
                        set_mining_state("failed");
                        return;
                    }
                    candidate = candidate.with_mining_nonce(*result.next_nonce());
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last_progress >= std::chrono::seconds{1}) {
                        emit("mining_progress", {{"attempts", mining_attempts_.load()}});
                        last_progress = now;
                    }
                }
                mining_active_.store(false);
                set_mining_state("cancelled");
                emit("mining_cancelled", {{"reason", "stale_parent"}});
            }
        };
    }

    void handle_block(
        const std::uint64_t peer_id,
        const crypto::ByteView payload,
        const bool submission
    ) {
        chain::BlockResult decoded = chain::deserialize_block(payload);
        if (!decoded.has_value()) {
            if (submission) {
                crypto::Hash256 empty{};
                send_block_result(peer_id, empty, false, 2);
            }
            return;
        }
        const chain::Block block = std::move(decoded).value();
        const crypto::Hash256 id = block.id();
        if (!chain_store_.has_value()) {
            bool extends_candidate = false;
            {
                std::lock_guard lock{mining_mutex_};
                extends_candidate = mining_parent_.has_value() &&
                    mining_height_.has_value() &&
                    block.previous_block_hash() == *mining_parent_ &&
                    block.height() == *mining_height_;
            }
            if (!extends_candidate ||
                block.chain_id() != core::network_parameters(config_.network_profile).chain_id ||
                consensus::validate_proof_of_work(block) !=
                    consensus::ProofOfWorkError::none) {
                return;
            }
            if (mining_active_.load()) {
                mining_cancelled_.store(true);
            }
            bool own_block = false;
            {
                std::lock_guard lock{mining_mutex_};
                own_block = found_block_id_.has_value() && *found_block_id_ == id;
            }
            if (own_block) {
                set_mining_state("accepted");
                emit("mining_accepted", {{"block_id", crypto::to_upper_hex(id)}});
            }
            return;
        }
        storage::ChainStoreAppendResult appended;
        {
            std::lock_guard lock{state_mutex_};
            appended = chain_store_->append(block);
        }
        if (!appended.has_value()) {
            if (submission) {
                const std::uint16_t reason =
                    appended.storage_error == storage::ChainStoreError::none &&
                    (appended.chain_result.error == chain::ChainError::unexpected_parent ||
                     appended.chain_result.error == chain::ChainError::unexpected_height)
                    ? 1 : 3;
                send_block_result(peer_id, id, false, reason);
            }
            emit("block_rejected", {
                {"block_id", crypto::to_upper_hex(id)},
                {"reason", appended.storage_error == storage::ChainStoreError::none
                    ? std::string{chain::chain_error_message(appended.chain_result.error)}
                    : std::string{storage::chain_store_error_message(appended.storage_error)}},
            });
            return;
        }
        if (submission) {
            send_block_result(peer_id, id, true, 0);
        }
        network_->broadcast(network::MessageType::block, block.serialize(), peer_id);
        emit("block_accepted", {
            {"block_id", crypto::to_upper_hex(id)},
            {"height", block.height()},
            {"peer_id", peer_id},
        });
        emit("block_relayed", {{"block_id", crypto::to_upper_hex(id)}});
    }

    void handle_block_result(const crypto::ByteView payload) {
        if (payload.size() != 35) {
            return;
        }
        if (payload[32] > 1) {
            return;
        }
        crypto::Hash256 result_block_id{};
        std::ranges::copy(payload.first(32), result_block_id.begin());
        {
            std::lock_guard lock{mining_mutex_};
            if (!found_block_id_.has_value() || *found_block_id_ != result_block_id) {
                return;
            }
        }
        const bool accepted = payload[32] == 1;
        const std::uint16_t reason = static_cast<std::uint16_t>(payload[33]) |
            static_cast<std::uint16_t>(payload[34]) << 8U;
        if ((accepted && reason != 0) || (!accepted && reason == 0)) {
            return;
        }
        set_mining_state(accepted ? "accepted" : reason == 1 ? "cancelled" : "rejected");
        emit(
            accepted ? "mining_accepted" : reason == 1
                ? "mining_cancelled"
                : "mining_rejected",
            accepted ? nlohmann::json{{"reason_code", reason}}
                : nlohmann::json{{"reason_code", reason}, {"reason", reason == 1
                    ? "stale_parent" : "invalid_block"}}
        );
    }

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
    EventEmitter* events_ = nullptr;
    mutable std::mutex state_mutex_;
    mutable std::mutex mining_mutex_;
    std::thread mining_thread_;
    std::atomic_bool mining_cancelled_{false};
    std::atomic_bool mining_active_{false};
    std::atomic_uint64_t mining_attempts_{0};
    std::string mining_state_ = "idle";
    std::uint64_t next_template_request_ = 1;
    std::optional<std::uint64_t> mining_source_peer_;
    std::optional<std::uint64_t> pending_template_request_;
    std::optional<crypto::Hash256> mining_parent_;
    std::optional<std::uint64_t> mining_height_;
    std::optional<crypto::Hash256> found_block_id_;
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
    if (method == "start_mining") {
        std::string error;
        if (!runtime.start_mining(error)) {
            return error_response(id, "mining_rejected", error);
        }
        return {{"id", id}, {"ok", true}, {"result", {{"started", true}}}};
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
