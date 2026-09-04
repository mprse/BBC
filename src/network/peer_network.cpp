#include "bbc/network/peer_network.hpp"

#include "bbc/network/protocol.hpp"
#include "crypto/sodium_runtime.hpp"

#include <asio.hpp>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace bbc::network {
namespace {

constexpr std::size_t maximum_outbound_queue_size = 4 * 1024 * 1024;
constexpr std::size_t maximum_peer_count = 64;
constexpr std::size_t maximum_connections_per_ip = 16;
constexpr auto handshake_timeout = std::chrono::seconds{5};
constexpr auto idle_ping_delay = std::chrono::seconds{30};
constexpr auto pong_timeout = std::chrono::seconds{10};
constexpr auto reconnect_initial_delay = std::chrono::milliseconds{250};
constexpr auto reconnect_maximum_delay = std::chrono::seconds{5};

std::string endpoint_text(const asio::ip::tcp::endpoint& endpoint) {
    return endpoint.address().to_string() + ':' + std::to_string(endpoint.port());
}

}  // namespace

class PeerNetwork::Impl final {
public:
    Impl(
        PeerNetworkConfig config,
        PeerEventHandler event_handler,
        PeerMessageHandler message_handler
    )
        : config_(std::move(config)),
          event_handler_(std::move(event_handler)),
          message_handler_(std::move(message_handler)),
          work_(asio::make_work_guard(context_)),
          acceptor_(context_) {
        crypto::detail::ensure_sodium_initialized();
        randombytes_buf(session_nonce_.data(), session_nonce_.size());
    }

    ~Impl() {
        stop();
    }

    [[nodiscard]] bool start(std::string& error) {
        try {
            if (config_.listen) {
                const asio::ip::tcp::endpoint endpoint{
                    asio::ip::make_address_v4("127.0.0.1"),
                    config_.listen_port,
                };
                acceptor_.open(endpoint.protocol());
                acceptor_.set_option(asio::socket_base::reuse_address{true});
                acceptor_.bind(endpoint);
                acceptor_.listen(asio::socket_base::max_listen_connections);
                listen_port_ = acceptor_.local_endpoint().port();
                accept_next();
            }
            thread_ = std::thread{[this] { context_.run(); }};
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    void stop() noexcept {
        if (!thread_.joinable()) {
            return;
        }
        asio::post(context_, [this] {
            stopping_ = true;
            asio::error_code ignored;
            acceptor_.close(ignored);
            for (auto& [id, session] : sessions_) {
                static_cast<void>(id);
                session->stop("node shutdown", false);
            }
            sessions_.clear();
            for (auto& [endpoint, target] : targets_) {
                static_cast<void>(endpoint);
                target->timer.cancel();
            }
            work_.reset();
        });
        thread_.join();
    }

    [[nodiscard]] std::uint16_t listen_port() const noexcept {
        return listen_port_;
    }

    [[nodiscard]] bool connect(
        const std::string_view host,
        const std::uint16_t port,
        std::string& error
    ) {
        asio::error_code address_error;
        const asio::ip::address address = asio::ip::make_address(host, address_error);
        if (address_error || !address.is_v4() || !address.to_v4().is_loopback() ||
            port == 0) {
            error = "P2P endpoint must use a nonzero 127.0.0.0/8 port.";
            return false;
        }
        const asio::ip::tcp::endpoint endpoint{address, port};
        asio::post(context_, [this, endpoint] { add_target(endpoint); });
        return true;
    }

    [[nodiscard]] std::uint64_t ping_all() {
        const std::uint64_t nonce = reserve_ping_nonce();
        asio::post(context_, [this, nonce] {
            for (const auto& [id, session] : sessions_) {
                static_cast<void>(id);
                session->send_ping(nonce);
            }
        });
        return nonce;
    }

    [[nodiscard]] bool send(
        const std::uint64_t peer_id,
        const MessageType type,
        crypto::Bytes payload
    ) {
        const std::vector<PeerStatus> snapshot = peers();
        const bool known = snapshot.end() != std::ranges::find_if(
            snapshot, [peer_id](const PeerStatus& peer) {
                return peer.id == peer_id && peer.handshake_complete;
            }
        );
        if (!known) {
            return false;
        }
        asio::post(context_, [this, peer_id, type, payload = std::move(payload)] {
            const auto found = sessions_.find(peer_id);
            if (found != sessions_.end()) {
                found->second->send_message(type, payload);
            }
        });
        return true;
    }

    void broadcast(
        const MessageType type,
        crypto::Bytes payload,
        const std::optional<std::uint64_t> excluded_peer
    ) {
        asio::post(context_, [this, type, payload = std::move(payload), excluded_peer] {
            for (const auto& [id, session] : sessions_) {
                if (id != excluded_peer && session->handshake_complete()) {
                    session->send_message(type, payload);
                }
            }
        });
    }

    [[nodiscard]] std::vector<PeerStatus> peers() const {
        std::lock_guard lock{state_mutex_};
        std::vector<PeerStatus> result;
        result.reserve(peer_status_.size());
        for (const auto& [id, status] : peer_status_) {
            static_cast<void>(id);
            result.push_back(status);
        }
        std::ranges::sort(result, {}, &PeerStatus::id);
        return result;
    }

private:
    struct Target;

    class Session final : public std::enable_shared_from_this<Session> {
    public:
        Session(
            Impl& owner,
            const std::uint64_t id,
            asio::ip::tcp::socket socket,
            const PeerDirection direction,
            std::shared_ptr<Target> target
        ) : owner_(owner),
            id_(id),
            socket_(std::move(socket)),
            direction_(direction),
            target_(std::move(target)),
            handshake_timer_(owner.context_),
            idle_timer_(owner.context_),
            pong_timer_(owner.context_) {}

        void start() {
            asio::error_code endpoint_error;
            const asio::ip::tcp::endpoint remote = socket_.remote_endpoint(endpoint_error);
            endpoint_ = endpoint_error ? "unknown" : endpoint_text(remote);
            remote_address_ = endpoint_error ? "unknown" : remote.address().to_string();
            owner_.record_new(*this);
            handshake_timer_.expires_after(handshake_timeout);
            handshake_timer_.async_wait([self = shared_from_this()](const asio::error_code& error) {
                if (!error && !self->handshake_complete_) {
                    self->stop("handshake timeout", true);
                }
            });
            send(MessageType::hello, serialize_hello(owner_.local_hello()));
            read_header();
        }

        void send_ping(const std::uint64_t nonce) {
            if (!handshake_complete_ || stopped_) {
                return;
            }
            last_ping_nonce_ = nonce;
            owner_.record_status(*this);
            send(MessageType::ping, serialize_ping_nonce(nonce));
        }

        void send_message(const MessageType type, const crypto::ByteView payload) {
            if (handshake_complete_ && !stopped_) {
                send(type, payload);
            }
        }

        void stop(
            const std::string& reason,
            const bool notify_owner,
            const bool reconnect = true
        ) {
            if (stopped_) {
                return;
            }
            stopped_ = true;
            handshake_timer_.cancel();
            idle_timer_.cancel();
            pong_timer_.cancel();
            asio::error_code ignored;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
            socket_.close(ignored);
            if (notify_owner) {
                owner_.session_stopped(shared_from_this(), reason, reconnect);
            }
        }

        [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
        [[nodiscard]] PeerDirection direction() const noexcept { return direction_; }
        [[nodiscard]] const std::string& endpoint() const noexcept { return endpoint_; }
        [[nodiscard]] const std::string& remote_address() const noexcept {
            return remote_address_;
        }
        [[nodiscard]] bool handshake_complete() const noexcept { return handshake_complete_; }
        [[nodiscard]] const std::optional<HelloPayload>& remote_hello() const noexcept {
            return remote_hello_;
        }
        [[nodiscard]] const std::shared_ptr<Target>& target() const noexcept {
            return target_;
        }

        void adopt_target(std::shared_ptr<Target> target) {
            target_ = std::move(target);
        }

        [[nodiscard]] PeerStatus status() const {
            PeerStatus result{
                id_,
                direction_,
                endpoint_,
                handshake_complete_,
            };
            if (remote_hello_.has_value()) {
                result.protocol_version = selected_protocol_version_;
                result.services = remote_hello_->services;
                result.remote_height = remote_hello_->tip_height;
                result.remote_tip = remote_hello_->tip_block_id;
            }
            result.last_ping_nonce = last_ping_nonce_;
            result.last_pong_nonce = last_pong_nonce_;
            return result;
        }

    private:
        void send(const MessageType type, const crypto::ByteView payload) {
            crypto::Bytes frame = serialize_frame(type, payload, owner_.config_.chain_id);
            if (frame.empty() || queued_bytes_ + frame.size() > maximum_outbound_queue_size) {
                stop("outbound queue limit exceeded", true);
                return;
            }
            queued_bytes_ += frame.size();
            const bool idle = outbound_.empty();
            outbound_.push_back(std::move(frame));
            if (idle) {
                write_next();
            }
        }

        void write_next() {
            if (outbound_.empty() || stopped_) {
                return;
            }
            asio::async_write(
                socket_,
                asio::buffer(outbound_.front()),
                [self = shared_from_this()](
                    const asio::error_code& error,
                    const std::size_t
                ) {
                    if (error) {
                        self->stop("socket write failed", true);
                        return;
                    }
                    self->queued_bytes_ -= self->outbound_.front().size();
                    self->outbound_.pop_front();
                    self->write_next();
                }
            );
        }

        void read_header() {
            asio::async_read(
                socket_,
                asio::buffer(header_buffer_),
                [self = shared_from_this()](
                    const asio::error_code& error,
                    const std::size_t
                ) {
                    if (error) {
                        self->stop("socket read failed", true);
                        return;
                    }
                    const FrameHeaderResult decoded = deserialize_frame_header(
                        self->header_buffer_,
                        self->owner_.config_.chain_id
                    );
                    if (!decoded.has_value()) {
                        self->stop(
                            std::string{protocol_error_message(decoded.error())},
                            true
                        );
                        return;
                    }
                    self->current_header_ = decoded.value();
                    self->payload_buffer_.assign(decoded.value().payload_length, 0);
                    self->read_payload();
                }
            );
        }

        void read_payload() {
            asio::async_read(
                socket_,
                asio::buffer(payload_buffer_),
                [self = shared_from_this()](
                    const asio::error_code& error,
                    const std::size_t
                ) {
                    if (error) {
                        self->stop("socket read failed", true);
                        return;
                    }
                    const ProtocolError payload_error = validate_frame_payload(
                        *self->current_header_,
                        self->payload_buffer_
                    );
                    if (payload_error != ProtocolError::none) {
                        self->stop(
                            std::string{protocol_error_message(payload_error)},
                            true
                        );
                        return;
                    }
                    if (!self->handle_message(
                            self->current_header_->type,
                            self->payload_buffer_
                        )) {
                        return;
                    }
                    if (self->handshake_complete_ && !self->liveness_nonce_.has_value()) {
                        self->arm_idle_timer();
                    }
                    self->current_header_.reset();
                    self->payload_buffer_.clear();
                    self->read_header();
                }
            );
        }

        [[nodiscard]] bool handle_message(
            const MessageType type,
            const crypto::ByteView payload
        ) {
            if (!remote_hello_.has_value()) {
                if (type != MessageType::hello) {
                    stop("HELLO must be the first message", true);
                    return false;
                }
                return handle_hello(payload);
            }
            if (type == MessageType::hello) {
                stop("duplicate HELLO", true);
                return false;
            }
            if (!handshake_complete_) {
                stop("message received before handshake completion", true);
                return false;
            }
            const std::uint64_t nonce = deserialize_ping_nonce(payload);
            if (type == MessageType::ping) {
                send(MessageType::pong, serialize_ping_nonce(nonce));
                return true;
            }
            if (type == MessageType::pong) {
                if (last_ping_nonce_.has_value() && *last_ping_nonce_ == nonce) {
                    last_pong_nonce_ = nonce;
                    owner_.record_status(*this);
                    owner_.emit(PeerEvent{"peer_pong", id_, endpoint_, std::to_string(nonce)});
                } else {
                    owner_.emit(PeerEvent{
                        "peer_unmatched_pong",
                        id_,
                        endpoint_,
                        std::to_string(nonce),
                    });
                }
                if (liveness_nonce_.has_value() && *liveness_nonce_ == nonce) {
                    liveness_nonce_.reset();
                    pong_timer_.cancel();
                    arm_idle_timer();
                }
                return true;
            }
            owner_.handle_application_message(id_, type, payload);
            return true;
        }

        [[nodiscard]] bool handle_hello(const crypto::ByteView payload) {
            const HelloPayloadResult decoded = deserialize_hello(payload);
            if (!decoded.has_value()) {
                stop(std::string{protocol_error_message(decoded.error())}, true);
                return false;
            }
            const HelloPayload& hello = decoded.value();
            if (hello.chain_id != owner_.config_.chain_id ||
                hello.genesis_block_id != owner_.config_.genesis_block_id) {
                stop("peer network identity mismatch", true);
                return false;
            }
            if (hello.session_nonce == owner_.session_nonce_) {
                stop("self-connection rejected", true);
                return false;
            }
            if ((hello.services & (full_node_service | mining_service)) == 0) {
                stop("peer does not advertise a supported service", true);
                return false;
            }
            const std::uint16_t minimum = std::max(
                hello.minimum_protocol_version,
                protocol_version
            );
            const std::uint16_t maximum = std::min(
                hello.maximum_protocol_version,
                protocol_version
            );
            if (minimum > maximum) {
                stop("no common protocol version", true);
                return false;
            }
            remote_hello_ = hello;
            selected_protocol_version_ = maximum;
            if (!owner_.complete_handshake(shared_from_this())) {
                return false;
            }
            handshake_complete_ = true;
            handshake_timer_.cancel();
            owner_.record_status(*this);
            owner_.emit(PeerEvent{"peer_handshake_complete", id_, endpoint_, {}});
            arm_idle_timer();
            return true;
        }

        void arm_idle_timer() {
            idle_timer_.expires_after(idle_ping_delay);
            idle_timer_.async_wait([self = shared_from_this()](const asio::error_code& error) {
                if (error || self->stopped_ || !self->handshake_complete_) {
                    return;
                }
                const std::uint64_t nonce = self->owner_.reserve_ping_nonce();
                self->liveness_nonce_ = nonce;
                self->send_ping(nonce);
                self->pong_timer_.expires_after(pong_timeout);
                self->pong_timer_.async_wait(
                    [self, nonce](const asio::error_code& pong_error) {
                        if (!pong_error && self->liveness_nonce_ == nonce) {
                            self->stop("PONG timeout", true);
                        }
                    }
                );
            });
        }

        Impl& owner_;
        std::uint64_t id_;
        asio::ip::tcp::socket socket_;
        PeerDirection direction_;
        std::shared_ptr<Target> target_;
        asio::steady_timer handshake_timer_;
        asio::steady_timer idle_timer_;
        asio::steady_timer pong_timer_;
        std::string endpoint_;
        std::string remote_address_;
        std::array<crypto::Byte, frame_header_size> header_buffer_{};
        std::optional<FrameHeader> current_header_;
        crypto::Bytes payload_buffer_;
        std::deque<crypto::Bytes> outbound_;
        std::size_t queued_bytes_ = 0;
        std::optional<HelloPayload> remote_hello_;
        std::uint16_t selected_protocol_version_ = 0;
        std::optional<std::uint64_t> last_ping_nonce_;
        std::optional<std::uint64_t> last_pong_nonce_;
        std::optional<std::uint64_t> liveness_nonce_;
        bool handshake_complete_ = false;
        bool stopped_ = false;
    };

    struct Target {
        Target(asio::io_context& context, asio::ip::tcp::endpoint value)
            : endpoint(std::move(value)), timer(context) {}

        asio::ip::tcp::endpoint endpoint;
        asio::steady_timer timer;
        std::chrono::milliseconds reconnect_delay{reconnect_initial_delay};
        std::optional<std::uint64_t> session_id;
        bool connecting = false;
    };

    [[nodiscard]] HelloPayload local_hello() const {
        return HelloPayload{
            protocol_version,
            protocol_version,
            config_.chain_id,
            config_.services,
            session_nonce_,
            config_.tip_height,
            config_.tip_block_id,
            config_.genesis_block_id,
        };
    }

    void accept_next() {
        if (stopping_ || sessions_.size() >= maximum_peer_count) {
            if (!stopping_) {
                accept_retry_timer_.emplace(context_);
                accept_retry_timer_->expires_after(std::chrono::milliseconds{100});
                accept_retry_timer_->async_wait([this](const asio::error_code& error) {
                    if (!error) {
                        accept_next();
                    }
                });
            }
            return;
        }
        acceptor_.async_accept([this](
            const asio::error_code& error,
            asio::ip::tcp::socket socket
        ) {
            if (!error) {
                asio::error_code endpoint_error;
                const auto remote = socket.remote_endpoint(endpoint_error);
                const std::string address = endpoint_error
                    ? std::string{}
                    : remote.address().to_string();
                const std::size_t address_count = std::ranges::count_if(
                    sessions_,
                    [&address](const auto& entry) {
                        return entry.second->direction() == PeerDirection::inbound &&
                            entry.second->remote_address() == address;
                    }
                );
                if (address_count >= maximum_connections_per_ip) {
                    asio::error_code ignored;
                    socket.close(ignored);
                    emit(PeerEvent{
                        "peer_rejected",
                        0,
                        endpoint_error ? std::string{} : endpoint_text(remote),
                        "per-IP connection limit exceeded",
                    });
                } else {
                    create_session(std::move(socket), PeerDirection::inbound, nullptr);
                }
            } else if (!stopping_) {
                emit(PeerEvent{"peer_accept_failed", 0, {}, error.message()});
            }
            accept_next();
        });
    }

    void add_target(const asio::ip::tcp::endpoint& endpoint) {
        const std::string key = endpoint_text(endpoint);
        auto [iterator, inserted] = targets_.try_emplace(
            key,
            std::make_shared<Target>(context_, endpoint)
        );
        if (inserted || (!iterator->second->connecting &&
                         !iterator->second->session_id.has_value())) {
            begin_connect(iterator->second);
        }
    }

    void begin_connect(const std::shared_ptr<Target>& target) {
        if (stopping_ || target->connecting || target->session_id.has_value() ||
            sessions_.size() >= maximum_peer_count) {
            return;
        }
        target->connecting = true;
        auto socket = std::make_shared<asio::ip::tcp::socket>(context_);
        socket->async_connect(target->endpoint, [this, target, socket](
            const asio::error_code& error
        ) {
            target->connecting = false;
            if (error) {
                emit(PeerEvent{
                    "peer_connect_failed",
                    0,
                    endpoint_text(target->endpoint),
                    error.message(),
                });
                schedule_reconnect(target);
                return;
            }
            const std::uint64_t id = create_session(
                std::move(*socket),
                PeerDirection::outbound,
                target
            );
            target->session_id = id;
        });
    }

    void schedule_reconnect(const std::shared_ptr<Target>& target) {
        if (stopping_ || target->connecting || target->session_id.has_value()) {
            return;
        }
        target->timer.expires_after(target->reconnect_delay);
        target->reconnect_delay = std::min(
            target->reconnect_delay * 2,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                reconnect_maximum_delay
            )
        );
        target->timer.async_wait([this, target](const asio::error_code& error) {
            if (!error) {
                begin_connect(target);
            }
        });
    }

    std::uint64_t create_session(
        asio::ip::tcp::socket socket,
        const PeerDirection direction,
        std::shared_ptr<Target> target
    ) {
        const std::uint64_t id = next_session_id_++;
        auto session = std::make_shared<Session>(
            *this,
            id,
            std::move(socket),
            direction,
            std::move(target)
        );
        sessions_.emplace(id, session);
        session->start();
        return id;
    }

    [[nodiscard]] bool complete_handshake(const std::shared_ptr<Session>& candidate) {
        const auto& remote_nonce = candidate->remote_hello()->session_nonce;
        std::shared_ptr<Session> replaced_session;
        for (const auto& [id, existing] : sessions_) {
            if (id == candidate->id() || !existing->remote_hello().has_value() ||
                existing->remote_hello()->session_nonce != remote_nonce) {
                continue;
            }
            const PeerDirection preferred = session_nonce_ < remote_nonce
                ? PeerDirection::outbound
                : PeerDirection::inbound;
            const bool candidate_preferred = candidate->direction() == preferred;
            const bool existing_preferred = existing->direction() == preferred;
            if (candidate_preferred && !existing_preferred) {
                replaced_session = existing;
                break;
            }
            if (!candidate_preferred || existing_preferred) {
                if (!existing->target() && candidate->target()) {
                    existing->adopt_target(candidate->target());
                    candidate->target()->session_id = existing->id();
                }
                candidate->stop("duplicate peer connection", true, false);
                return false;
            }
        }
        if (replaced_session) {
            if (!candidate->target() && replaced_session->target()) {
                candidate->adopt_target(replaced_session->target());
                replaced_session->target()->session_id = candidate->id();
            }
            replaced_session->stop("duplicate peer connection", true, false);
        }
        if (candidate->target()) {
            candidate->target()->reconnect_delay = reconnect_initial_delay;
        }
        return true;
    }

    void session_stopped(
        const std::shared_ptr<Session>& session,
        const std::string& reason,
        const bool reconnect
    ) {
        emit(PeerEvent{"peer_disconnected", session->id(), session->endpoint(), reason});
        {
            std::lock_guard lock{state_mutex_};
            peer_status_.erase(session->id());
        }
        const std::shared_ptr<Target> target = session->target();
        sessions_.erase(session->id());
        if (target && target->session_id == session->id()) {
            target->session_id.reset();
            if (reconnect) {
                schedule_reconnect(target);
            }
        }
    }

    void record_new(const Session& session) {
        record_status(session);
        emit(PeerEvent{"peer_connected", session.id(), session.endpoint(), {}});
    }

    void record_status(const Session& session) {
        std::lock_guard lock{state_mutex_};
        peer_status_[session.id()] = session.status();
    }

    void emit(const PeerEvent& event) const {
        if (event_handler_) {
            event_handler_(event);
        }
    }

    void handle_application_message(
        const std::uint64_t peer_id,
        const MessageType type,
        const crypto::ByteView payload
    ) const {
        if (message_handler_) {
            message_handler_(peer_id, type, crypto::Bytes{payload.begin(), payload.end()});
        }
    }

    [[nodiscard]] std::uint64_t reserve_ping_nonce() {
        std::lock_guard lock{state_mutex_};
        return next_ping_nonce_++;
    }

    PeerNetworkConfig config_;
    PeerEventHandler event_handler_;
    PeerMessageHandler message_handler_;
    asio::io_context context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    asio::ip::tcp::acceptor acceptor_;
    std::optional<asio::steady_timer> accept_retry_timer_;
    std::thread thread_;
    std::array<crypto::Byte, 16> session_nonce_{};
    std::uint16_t listen_port_ = 0;
    bool stopping_ = false;
    std::uint64_t next_session_id_ = 1;
    std::unordered_map<std::uint64_t, std::shared_ptr<Session>> sessions_;
    std::unordered_map<std::string, std::shared_ptr<Target>> targets_;

    mutable std::mutex state_mutex_;
    std::unordered_map<std::uint64_t, PeerStatus> peer_status_;
    std::uint64_t next_ping_nonce_ = 1;
};

PeerNetwork::PeerNetwork(
    PeerNetworkConfig config,
    PeerEventHandler event_handler,
    PeerMessageHandler message_handler
) : impl_(std::make_unique<Impl>(
        std::move(config),
        std::move(event_handler),
        std::move(message_handler)
    )) {}

PeerNetwork::~PeerNetwork() = default;

bool PeerNetwork::start(std::string& error) {
    return impl_->start(error);
}

void PeerNetwork::stop() noexcept {
    impl_->stop();
}

std::uint16_t PeerNetwork::listen_port() const noexcept {
    return impl_->listen_port();
}

bool PeerNetwork::connect(
    const std::string_view host,
    const std::uint16_t port,
    std::string& error
) {
    return impl_->connect(host, port, error);
}

std::uint64_t PeerNetwork::ping_all() {
    return impl_->ping_all();
}

bool PeerNetwork::send(
    const std::uint64_t peer_id,
    const MessageType type,
    const crypto::ByteView payload
) {
    return impl_->send(peer_id, type, crypto::Bytes{payload.begin(), payload.end()});
}

void PeerNetwork::broadcast(
    const MessageType type,
    const crypto::ByteView payload,
    const std::optional<std::uint64_t> excluded_peer
) {
    impl_->broadcast(type, crypto::Bytes{payload.begin(), payload.end()}, excluded_peer);
}

std::vector<PeerStatus> PeerNetwork::peers() const {
    return impl_->peers();
}

std::string_view peer_direction_name(const PeerDirection direction) noexcept {
    return direction == PeerDirection::inbound ? "inbound" : "outbound";
}

}  // namespace bbc::network
