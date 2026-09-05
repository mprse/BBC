#pragma once

#include "bbc/crypto/types.hpp"
#include "bbc/network/protocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bbc::network {

enum class PeerDirection {
    inbound,
    outbound,
};

struct PeerStatus {
    std::uint64_t id = 0;
    PeerDirection direction = PeerDirection::inbound;
    std::string endpoint;
    bool handshake_complete = false;
    std::uint16_t protocol_version = 0;
    std::uint64_t services = 0;
    std::uint64_t remote_height = 0;
    crypto::Hash256 remote_tip{};
    std::optional<std::uint64_t> last_ping_nonce;
    std::optional<std::uint64_t> last_pong_nonce;
};

struct PeerEvent {
    std::string type;
    std::uint64_t peer_id = 0;
    std::string endpoint;
    std::string detail;
};

struct PeerNetworkConfig {
    std::uint16_t listen_port = 0;
    bool listen = true;
    std::uint32_t chain_id = 0;
    std::uint64_t services = 0;
    std::uint64_t tip_height = 0;
    crypto::Hash256 tip_block_id{};
    crypto::Hash256 genesis_block_id{};
};

using PeerEventHandler = std::function<void(const PeerEvent&)>;
using PeerMessageHandler = std::function<void(
    std::uint64_t,
    MessageType,
    const crypto::Bytes&
)>;

class PeerNetwork final {
public:
    PeerNetwork(
        PeerNetworkConfig config,
        PeerEventHandler event_handler,
        PeerMessageHandler message_handler = {}
    );
    PeerNetwork(const PeerNetwork&) = delete;
    PeerNetwork& operator=(const PeerNetwork&) = delete;
    ~PeerNetwork();

    [[nodiscard]] bool start(std::string& error);
    void stop() noexcept;

    [[nodiscard]] std::uint16_t listen_port() const noexcept;
    [[nodiscard]] bool connect(
        std::string_view host,
        std::uint16_t port,
        std::string& error
    );
    [[nodiscard]] bool disconnect(
        std::string_view host,
        std::uint16_t port,
        std::string& error
    );
    [[nodiscard]] std::uint64_t ping_all();
    void update_tip(std::uint64_t height, const crypto::Hash256& block_id);
    [[nodiscard]] bool send(
        std::uint64_t peer_id,
        MessageType type,
        crypto::ByteView payload
    );
    void broadcast(
        MessageType type,
        crypto::ByteView payload,
        std::optional<std::uint64_t> excluded_peer = std::nullopt
    );
    [[nodiscard]] std::vector<PeerStatus> peers() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view peer_direction_name(PeerDirection direction) noexcept;

}  // namespace bbc::network
