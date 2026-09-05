#pragma once

#include "bbc/crypto/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace bbc::network {

inline constexpr std::size_t frame_header_size = 44;
inline constexpr std::size_t maximum_frame_payload_size = 1024 * 1024;
inline constexpr std::size_t hello_payload_size = 104;
inline constexpr std::size_t ping_payload_size = 8;
inline constexpr std::uint16_t wire_version = 1;
inline constexpr std::uint16_t protocol_version = 1;
inline constexpr std::uint64_t full_node_service = 1ULL << 0U;
inline constexpr std::uint64_t mining_service = 1ULL << 1U;

enum class MessageType : std::uint16_t {
    hello = 1,
    ping = 2,
    pong = 3,
    template_request = 10,
    block_template = 11,
    block_submit = 12,
    block = 13,
    block_result = 14,
    transaction_submit = 20,
    transaction = 21,
    transaction_result = 22,
};

enum class ProtocolError {
    none,
    invalid_header_size,
    invalid_magic,
    unsupported_wire_version,
    unknown_message_type,
    oversized_payload,
    invalid_payload_size,
    invalid_checksum,
    invalid_protocol_range,
};

struct FrameHeader {
    MessageType type;
    std::uint32_t payload_length;
    crypto::Hash256 payload_checksum;
};

struct HelloPayload {
    std::uint16_t minimum_protocol_version = protocol_version;
    std::uint16_t maximum_protocol_version = protocol_version;
    std::uint32_t chain_id = 0;
    std::uint64_t services = 0;
    std::array<crypto::Byte, 16> session_nonce{};
    std::uint64_t tip_height = 0;
    crypto::Hash256 tip_block_id{};
    crypto::Hash256 genesis_block_id{};
};

class FrameHeaderResult final {
public:
    explicit FrameHeaderResult(FrameHeader header);
    explicit FrameHeaderResult(ProtocolError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] const FrameHeader& value() const&;
    [[nodiscard]] ProtocolError error() const noexcept;

private:
    std::variant<FrameHeader, ProtocolError> value_;
};

class HelloPayloadResult final {
public:
    explicit HelloPayloadResult(HelloPayload hello);
    explicit HelloPayloadResult(ProtocolError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] const HelloPayload& value() const&;
    [[nodiscard]] ProtocolError error() const noexcept;

private:
    std::variant<HelloPayload, ProtocolError> value_;
};

[[nodiscard]] crypto::Bytes serialize_frame(
    MessageType type,
    crypto::ByteView payload,
    std::uint32_t chain_id = 1
);
[[nodiscard]] FrameHeaderResult deserialize_frame_header(
    crypto::ByteView encoded,
    std::uint32_t chain_id = 1
);
[[nodiscard]] ProtocolError validate_frame_payload(
    const FrameHeader& header,
    crypto::ByteView payload
);

[[nodiscard]] crypto::Bytes serialize_hello(const HelloPayload& hello);
[[nodiscard]] HelloPayloadResult deserialize_hello(crypto::ByteView encoded);
[[nodiscard]] crypto::Bytes serialize_ping_nonce(std::uint64_t nonce);
[[nodiscard]] std::uint64_t deserialize_ping_nonce(crypto::ByteView encoded);
[[nodiscard]] std::string_view protocol_error_message(ProtocolError error) noexcept;

}  // namespace bbc::network
