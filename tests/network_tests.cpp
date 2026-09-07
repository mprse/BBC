#include "bbc/network/protocol.hpp"

#include "bbc/chain/block.hpp"
#include "bbc/crypto/hash.hpp"
#include "bbc/network/peer_network.hpp"
#include "bbc/transaction/transaction.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace {

struct ReceivedFrame {
    bbc::network::FrameHeader header;
    bbc::crypto::Bytes payload;
};

ReceivedFrame receive_frame(asio::ip::tcp::socket& socket) {
    std::array<bbc::crypto::Byte, bbc::network::frame_header_size> header_bytes{};
    asio::read(socket, asio::buffer(header_bytes));
    const bbc::network::FrameHeaderResult decoded =
        bbc::network::deserialize_frame_header(header_bytes);
    REQUIRE(decoded.has_value());
    bbc::crypto::Bytes payload(decoded.value().payload_length);
    asio::read(socket, asio::buffer(payload));
    REQUIRE(
        bbc::network::validate_frame_payload(decoded.value(), payload) ==
        bbc::network::ProtocolError::none
    );
    return ReceivedFrame{decoded.value(), std::move(payload)};
}

bool wait_for_peer_count(
    const bbc::network::PeerNetwork& network,
    const std::size_t expected
) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto peers = network.peers();
        if (peers.size() == expected &&
            std::ranges::all_of(peers, &bbc::network::PeerStatus::handshake_complete)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

bbc::network::PeerNetworkConfig test_network_config() {
    const bbc::chain::Block genesis = bbc::chain::genesis_block();
    return {
        0,
        true,
        genesis.chain_id(),
        bbc::network::full_node_service,
        genesis.height(),
        genesis.id(),
        genesis.id(),
    };
}

bool rejected_after_frames(
    const std::function<std::vector<bbc::crypto::Bytes>(
        bbc::network::HelloPayload
    )>& make_frames,
    const std::string_view expected_detail
) {
    std::mutex event_mutex;
    std::vector<bbc::network::PeerEvent> events;
    bbc::network::PeerNetwork server{
        test_network_config(),
        [&event_mutex, &events](const bbc::network::PeerEvent& event) {
            std::lock_guard lock{event_mutex};
            events.push_back(event);
        },
    };
    std::string start_error;
    if (!server.start(start_error)) {
        return false;
    }

    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    socket.connect({asio::ip::make_address_v4("127.0.0.1"), server.listen_port()});
    const ReceivedFrame local_frame = receive_frame(socket);
    const auto local_hello = bbc::network::deserialize_hello(local_frame.payload);
    if (!local_hello.has_value()) {
        return false;
    }
    for (const bbc::crypto::Bytes& frame : make_frames(local_hello.value())) {
        asio::write(socket, asio::buffer(frame));
    }

    bool found = false;
    for (int attempt = 0; attempt < 100 && !found; ++attempt) {
        {
            std::lock_guard lock{event_mutex};
            found = std::ranges::any_of(events, [expected_detail](const auto& event) {
                return event.type == "peer_disconnected" &&
                    event.detail == expected_detail;
            });
        }
        if (!found) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }
    server.stop();
    return found;
}

}  // namespace

TEST_CASE("P2P HELLO has a canonical frame encoding", "[network]") {
    bbc::network::HelloPayload hello{};
    hello.chain_id = 1;
    hello.services = bbc::network::full_node_service;
    hello.session_nonce[0] = 0xAA;
    hello.tip_height = 7;
    hello.tip_block_id[0] = 0xBB;
    hello.genesis_block_id[31] = 0xCC;

    const bbc::crypto::Bytes payload = bbc::network::serialize_hello(hello);
    const bbc::crypto::Bytes frame = bbc::network::serialize_frame(
        bbc::network::MessageType::hello,
        payload
    );

    REQUIRE(payload.size() == bbc::network::hello_payload_size);
    REQUIRE(frame.size() == bbc::network::frame_header_size + payload.size());
    CHECK(frame[0] == 'B');
    CHECK(frame[1] == 'B');
    CHECK(frame[2] == 'C');
    CHECK(frame[3] == 1);
    CHECK(frame[4] == 1);
    CHECK(frame[5] == 0);
    CHECK(frame[6] == 1);
    CHECK(frame[7] == 0);
    CHECK(frame[8] == 104);
    CHECK(frame[9] == 0);
    CHECK(frame[10] == 0);
    CHECK(frame[11] == 0);
    CHECK(std::ranges::equal(
        bbc::crypto::sha256(payload),
        std::span{frame}.subspan(12, bbc::crypto::hash256_size)
    ));
    CHECK(
        bbc::crypto::to_upper_hex(std::span{frame}.subspan(12, 32)) ==
        "D7DC7F1F0D28DCAC0AA111CDC357E1EDB8B72C81177C4FBE9CB8866A320A56F1"
    );

    const bbc::network::FrameHeaderResult header =
        bbc::network::deserialize_frame_header(
            std::span{frame}.first(bbc::network::frame_header_size)
        );
    REQUIRE(header.has_value());
    CHECK(header.value().type == bbc::network::MessageType::hello);
    CHECK(header.value().payload_length == bbc::network::hello_payload_size);
    CHECK(
        bbc::network::validate_frame_payload(header.value(), payload) ==
        bbc::network::ProtocolError::none
    );

    const bbc::network::HelloPayloadResult decoded =
        bbc::network::deserialize_hello(payload);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().chain_id == 1);
    CHECK(decoded.value().services == bbc::network::full_node_service);
    CHECK(decoded.value().session_nonce[0] == 0xAA);
    CHECK(decoded.value().tip_height == 7);
    CHECK(decoded.value().tip_block_id[0] == 0xBB);
    CHECK(decoded.value().genesis_block_id[31] == 0xCC);
}

TEST_CASE("P2P PING and PONG preserve their nonce", "[network]") {
    constexpr std::uint64_t nonce = 0x1122334455667788ULL;
    const bbc::crypto::Bytes payload = bbc::network::serialize_ping_nonce(nonce);
    CHECK(payload == bbc::crypto::Bytes{0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11});
    CHECK(bbc::network::deserialize_ping_nonce(payload) == nonce);

    for (const bbc::network::MessageType type : {
             bbc::network::MessageType::ping,
             bbc::network::MessageType::pong,
         }) {
        const bbc::crypto::Bytes frame = bbc::network::serialize_frame(type, payload);
        const bbc::network::FrameHeaderResult header =
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            );
        REQUIRE(header.has_value());
        CHECK(header.value().type == type);
    }
}

TEST_CASE("P2P transaction messages enforce canonical payload sizes", "[network]") {
    const bbc::crypto::Bytes transaction(
        bbc::transaction::signed_transaction_size,
        0xA5
    );
    for (const bbc::network::MessageType type : {
             bbc::network::MessageType::transaction_submit,
             bbc::network::MessageType::transaction,
         }) {
        const bbc::crypto::Bytes frame = bbc::network::serialize_frame(
            type,
            transaction
        );
        REQUIRE_FALSE(frame.empty());
        const auto header = bbc::network::deserialize_frame_header(
            std::span{frame}.first(bbc::network::frame_header_size)
        );
        REQUIRE(header.has_value());
        CHECK(header.value().type == type);
        CHECK(header.value().payload_length == transaction.size());
    }

    const bbc::crypto::Bytes result(
        bbc::crypto::hash256_size + 3,
        0x5A
    );
    CHECK_FALSE(bbc::network::serialize_frame(
        bbc::network::MessageType::transaction_result,
        result
    ).empty());
    CHECK(bbc::network::serialize_frame(
        bbc::network::MessageType::transaction,
        bbc::crypto::ByteView{transaction}.first(transaction.size() - 1)
    ).empty());
}

TEST_CASE("P2P synchronization messages enforce bounded payload sizes", "[network]") {
    const bbc::crypto::Bytes request(
        bbc::network::get_blocks_payload_minimum_size,
        0
    );
    CHECK_FALSE(bbc::network::serialize_frame(
        bbc::network::MessageType::get_blocks,
        request
    ).empty());
    CHECK(bbc::network::serialize_frame(
        bbc::network::MessageType::get_blocks,
        bbc::crypto::ByteView{request}.first(request.size() - 1)
    ).empty());
    const bbc::crypto::Bytes oversized_request(
        bbc::network::get_blocks_payload_maximum_size + 1,
        0
    );
    CHECK(bbc::network::serialize_frame(
        bbc::network::MessageType::get_blocks,
        oversized_request
    ).empty());

    const bbc::crypto::Bytes empty_response(
        bbc::network::blocks_payload_minimum_size,
        0
    );
    CHECK_FALSE(bbc::network::serialize_frame(
        bbc::network::MessageType::blocks,
        empty_response
    ).empty());
    CHECK(bbc::network::serialize_frame(
        bbc::network::MessageType::blocks,
        bbc::crypto::ByteView{empty_response}.first(empty_response.size() - 1)
    ).empty());
}

TEST_CASE("P2P frame headers reject malformed input before allocation", "[network]") {
    const bbc::crypto::Bytes payload = bbc::network::serialize_ping_nonce(5);
    bbc::crypto::Bytes frame = bbc::network::serialize_frame(
        bbc::network::MessageType::ping,
        payload
    );

    SECTION("wrong header size") {
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size - 1)
            ).error() == bbc::network::ProtocolError::invalid_header_size
        );
    }
    SECTION("bad magic") {
        frame[0] = 0;
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            ).error() == bbc::network::ProtocolError::invalid_magic
        );
    }
    SECTION("unsupported wire version") {
        frame[4] = 2;
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            ).error() == bbc::network::ProtocolError::unsupported_wire_version
        );
    }
    SECTION("unknown type") {
        frame[6] = 99;
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            ).error() == bbc::network::ProtocolError::unknown_message_type
        );
    }
    SECTION("oversized length") {
        frame[8] = 1;
        frame[9] = 0;
        frame[10] = 16;
        frame[11] = 0;
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            ).error() == bbc::network::ProtocolError::oversized_payload
        );
    }
    SECTION("wrong fixed payload length") {
        frame[8] = 7;
        CHECK(
            bbc::network::deserialize_frame_header(
                std::span{frame}.first(bbc::network::frame_header_size)
            ).error() == bbc::network::ProtocolError::invalid_payload_size
        );
    }
    SECTION("bad checksum") {
        frame[12] ^= 0xFF;
        const auto header = bbc::network::deserialize_frame_header(
            std::span{frame}.first(bbc::network::frame_header_size)
        );
        REQUIRE(header.has_value());
        CHECK(
            bbc::network::validate_frame_payload(header.value(), payload) ==
            bbc::network::ProtocolError::invalid_checksum
        );
    }
}

TEST_CASE("P2P HELLO rejects an invalid protocol range", "[network]") {
    bbc::network::HelloPayload hello{};
    hello.minimum_protocol_version = 2;
    hello.maximum_protocol_version = 1;
    CHECK(
        bbc::network::deserialize_hello(bbc::network::serialize_hello(hello)).error() ==
        bbc::network::ProtocolError::invalid_protocol_range
    );
}

TEST_CASE("P2P session handles fragmented and coalesced TCP frames", "[network]") {
    bbc::network::PeerNetwork server{test_network_config(), {}};
    std::string start_error;
    REQUIRE(server.start(start_error));

    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    socket.connect({asio::ip::make_address_v4("127.0.0.1"), server.listen_port()});

    const ReceivedFrame local_hello = receive_frame(socket);
    REQUIRE(local_hello.header.type == bbc::network::MessageType::hello);
    const auto decoded_local = bbc::network::deserialize_hello(local_hello.payload);
    REQUIRE(decoded_local.has_value());

    bbc::network::HelloPayload remote_hello = decoded_local.value();
    remote_hello.session_nonce.fill(0xA5);
    const bbc::crypto::Bytes hello_frame = bbc::network::serialize_frame(
        bbc::network::MessageType::hello,
        bbc::network::serialize_hello(remote_hello)
    );
    for (const bbc::crypto::Byte byte : hello_frame) {
        asio::write(socket, asio::buffer(&byte, 1));
    }
    REQUIRE(wait_for_peer_count(server, 1));

    const bbc::crypto::Bytes first_ping = bbc::network::serialize_frame(
        bbc::network::MessageType::ping,
        bbc::network::serialize_ping_nonce(11)
    );
    const bbc::crypto::Bytes second_ping = bbc::network::serialize_frame(
        bbc::network::MessageType::ping,
        bbc::network::serialize_ping_nonce(22)
    );
    bbc::crypto::Bytes combined = first_ping;
    combined.insert(combined.end(), second_ping.begin(), second_ping.end());
    asio::write(socket, asio::buffer(combined));

    const ReceivedFrame first_pong = receive_frame(socket);
    const ReceivedFrame second_pong = receive_frame(socket);
    CHECK(first_pong.header.type == bbc::network::MessageType::pong);
    CHECK(second_pong.header.type == bbc::network::MessageType::pong);
    CHECK(bbc::network::deserialize_ping_nonce(first_pong.payload) == 11);
    CHECK(bbc::network::deserialize_ping_nonce(second_pong.payload) == 22);

    server.stop();
}

TEST_CASE("an outbound peer target can be disconnected without reconnecting", "[network]") {
    bbc::network::PeerNetwork server{test_network_config(), {}};
    bbc::network::PeerNetwork client{test_network_config(), {}};
    std::string error;
    REQUIRE(server.start(error));
    REQUIRE(client.start(error));
    REQUIRE(client.connect("127.0.0.1", server.listen_port(), error));
    REQUIRE(wait_for_peer_count(server, 1));
    REQUIRE(wait_for_peer_count(client, 1));

    REQUIRE(client.disconnect("127.0.0.1", server.listen_port(), error));
    REQUIRE(wait_for_peer_count(client, 0));
    REQUIRE(wait_for_peer_count(server, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds{350});
    CHECK(client.peers().empty());
    CHECK(server.peers().empty());

    client.stop();
    server.stop();
}

TEST_CASE("P2P address scopes separate scenarios from LAN nodes", "[network]") {
    using bbc::network::PeerAddressScope;

    CHECK(bbc::network::peer_address_allowed("127.0.0.1", PeerAddressScope::loopback));
    CHECK(bbc::network::peer_address_allowed("127.0.0.2", PeerAddressScope::loopback));
    CHECK_FALSE(bbc::network::peer_address_allowed("10.0.0.2", PeerAddressScope::loopback));

    CHECK(bbc::network::peer_address_allowed("10.0.0.2", PeerAddressScope::private_network));
    CHECK(bbc::network::peer_address_allowed("172.16.0.1", PeerAddressScope::private_network));
    CHECK(bbc::network::peer_address_allowed("172.31.255.254", PeerAddressScope::private_network));
    CHECK(bbc::network::peer_address_allowed("192.168.1.20", PeerAddressScope::private_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("172.32.0.1", PeerAddressScope::private_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("0.0.0.0", PeerAddressScope::private_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("8.8.8.8", PeerAddressScope::private_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("seed.example.org", PeerAddressScope::private_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("::1", PeerAddressScope::private_network));

    CHECK(bbc::network::peer_address_allowed("8.8.8.8", PeerAddressScope::public_network));
    CHECK(bbc::network::peer_address_allowed("192.168.1.20", PeerAddressScope::public_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("0.0.0.0", PeerAddressScope::public_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("224.0.0.1", PeerAddressScope::public_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("255.255.255.255", PeerAddressScope::public_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("seed.example.org", PeerAddressScope::public_network));
    CHECK_FALSE(bbc::network::peer_address_allowed("::1", PeerAddressScope::public_network));

    bbc::network::PeerNetworkConfig internet_config = test_network_config();
    internet_config.listen = false;
    internet_config.address_scope = PeerAddressScope::public_network;
    bbc::network::PeerNetwork internet_network{std::move(internet_config), {}};
    std::string error;
    CHECK(internet_network.connect("203.0.113.10", 7333, error));
    CHECK_FALSE(internet_network.connect("0.0.0.0", 7333, error));
}

TEST_CASE("P2P listener binds its configured loopback address", "[network]") {
    bbc::network::PeerNetworkConfig config = test_network_config();
    config.listen_host = "127.0.0.2";
    bbc::network::PeerNetwork server{std::move(config), {}};
    std::string error;
    REQUIRE(server.start(error));

    asio::io_context context;
    asio::ip::tcp::socket socket{context};
    socket.connect({asio::ip::make_address_v4("127.0.0.2"), server.listen_port()});
    const ReceivedFrame hello = receive_frame(socket);
    CHECK(hello.header.type == bbc::network::MessageType::hello);

    server.stop();
}

TEST_CASE("P2P handshake rejects invalid ordering and identity", "[network]") {
    SECTION("PING before HELLO") {
        CHECK(rejected_after_frames(
            [](const bbc::network::HelloPayload&)
                -> std::vector<bbc::crypto::Bytes> {
                return {bbc::network::serialize_frame(
                    bbc::network::MessageType::ping,
                    bbc::network::serialize_ping_nonce(1)
                )};
            },
            "HELLO must be the first message"
        ));
    }
    SECTION("different chain") {
        CHECK(rejected_after_frames(
            [](bbc::network::HelloPayload hello)
                -> std::vector<bbc::crypto::Bytes> {
                ++hello.chain_id;
                hello.session_nonce.fill(0xA5);
                return {bbc::network::serialize_frame(
                    bbc::network::MessageType::hello,
                    bbc::network::serialize_hello(hello)
                )};
            },
            "peer network identity mismatch"
        ));
    }
    SECTION("no common protocol version") {
        CHECK(rejected_after_frames(
            [](bbc::network::HelloPayload hello)
                -> std::vector<bbc::crypto::Bytes> {
                hello.minimum_protocol_version = 2;
                hello.maximum_protocol_version = 2;
                hello.session_nonce.fill(0xA5);
                return {bbc::network::serialize_frame(
                    bbc::network::MessageType::hello,
                    bbc::network::serialize_hello(hello)
                )};
            },
            "no common protocol version"
        ));
    }
    SECTION("missing supported service") {
        CHECK(rejected_after_frames(
            [](bbc::network::HelloPayload hello)
                -> std::vector<bbc::crypto::Bytes> {
                hello.services = 0;
                hello.session_nonce.fill(0xA5);
                return {bbc::network::serialize_frame(
                    bbc::network::MessageType::hello,
                    bbc::network::serialize_hello(hello)
                )};
            },
            "peer does not advertise a supported service"
        ));
    }
    SECTION("self connection") {
        CHECK(rejected_after_frames(
            [](const bbc::network::HelloPayload& hello)
                -> std::vector<bbc::crypto::Bytes> {
                return {bbc::network::serialize_frame(
                    bbc::network::MessageType::hello,
                    bbc::network::serialize_hello(hello)
                )};
            },
            "self-connection rejected"
        ));
    }
    SECTION("duplicate HELLO") {
        CHECK(rejected_after_frames(
            [](bbc::network::HelloPayload hello)
                -> std::vector<bbc::crypto::Bytes> {
                hello.session_nonce.fill(0xA5);
                const bbc::crypto::Bytes frame = bbc::network::serialize_frame(
                    bbc::network::MessageType::hello,
                    bbc::network::serialize_hello(hello)
                );
                return {frame, frame};
            },
            "duplicate HELLO"
        ));
    }
}

TEST_CASE("network profiles use isolated P2P magic", "[network]") {
    const bbc::crypto::Bytes payload = bbc::network::serialize_ping_nonce(7);
    const bbc::crypto::Bytes development = bbc::network::serialize_frame(
        bbc::network::MessageType::ping, payload, 1
    );
    const bbc::crypto::Bytes regtest = bbc::network::serialize_frame(
        bbc::network::MessageType::ping, payload, 2
    );
    REQUIRE(development.size() == regtest.size());
    CHECK(development[3] == 1);
    CHECK(regtest[3] == 2);
    CHECK_FALSE(bbc::network::deserialize_frame_header(
        std::span{regtest}.first(bbc::network::frame_header_size), 1
    ).has_value());
    CHECK(bbc::network::deserialize_frame_header(
        std::span{regtest}.first(bbc::network::frame_header_size), 2
    ).has_value());
}
