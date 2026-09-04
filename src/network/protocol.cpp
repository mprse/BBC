#include "bbc/network/protocol.hpp"

#include "bbc/crypto/hash.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace bbc::network {
namespace {

constexpr std::array<crypto::Byte, 4> network_magic{'B', 'B', 'C', 1};

template <typename Range>
void append(crypto::Bytes& output, const Range& range) {
    output.insert(output.end(), range.begin(), range.end());
}

void append_u16_le(crypto::Bytes& output, const std::uint16_t value) {
    output.push_back(static_cast<crypto::Byte>(value & 0xFFU));
    output.push_back(static_cast<crypto::Byte>((value >> 8U) & 0xFFU));
}

void append_u32_le(crypto::Bytes& output, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

void append_u64_le(crypto::Bytes& output, const std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

std::uint16_t read_u16_le(const crypto::ByteView input, const std::size_t offset) {
    return static_cast<std::uint16_t>(input[offset]) |
        static_cast<std::uint16_t>(input[offset + 1]) << 8U;
}

std::uint32_t read_u32_le(const crypto::ByteView input, const std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64_le(const crypto::ByteView input, const std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
std::array<crypto::Byte, Size> read_array(
    const crypto::ByteView input,
    const std::size_t offset
) {
    std::array<crypto::Byte, Size> result{};
    std::ranges::copy(input.subspan(offset, Size), result.begin());
    return result;
}

std::optional<MessageType> decode_message_type(const std::uint16_t value) {
    switch (value) {
        case static_cast<std::uint16_t>(MessageType::hello):
            return MessageType::hello;
        case static_cast<std::uint16_t>(MessageType::ping):
            return MessageType::ping;
        case static_cast<std::uint16_t>(MessageType::pong):
            return MessageType::pong;
        default:
            return std::nullopt;
    }
}

std::size_t expected_payload_size(const MessageType type) {
    return type == MessageType::hello ? hello_payload_size : ping_payload_size;
}

}  // namespace

FrameHeaderResult::FrameHeaderResult(FrameHeader header)
    : value_(std::move(header)) {}

FrameHeaderResult::FrameHeaderResult(const ProtocolError error) : value_(error) {}

bool FrameHeaderResult::has_value() const noexcept {
    return std::holds_alternative<FrameHeader>(value_);
}

const FrameHeader& FrameHeaderResult::value() const& {
    return std::get<FrameHeader>(value_);
}

ProtocolError FrameHeaderResult::error() const noexcept {
    const auto* error = std::get_if<ProtocolError>(&value_);
    return error == nullptr ? ProtocolError::none : *error;
}

HelloPayloadResult::HelloPayloadResult(HelloPayload hello)
    : value_(std::move(hello)) {}

HelloPayloadResult::HelloPayloadResult(const ProtocolError error) : value_(error) {}

bool HelloPayloadResult::has_value() const noexcept {
    return std::holds_alternative<HelloPayload>(value_);
}

const HelloPayload& HelloPayloadResult::value() const& {
    return std::get<HelloPayload>(value_);
}

ProtocolError HelloPayloadResult::error() const noexcept {
    const auto* error = std::get_if<ProtocolError>(&value_);
    return error == nullptr ? ProtocolError::none : *error;
}

crypto::Bytes serialize_frame(
    const MessageType type,
    const crypto::ByteView payload
) {
    if (payload.size() > maximum_frame_payload_size ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    crypto::Bytes encoded;
    encoded.reserve(frame_header_size + payload.size());
    append(encoded, network_magic);
    append_u16_le(encoded, wire_version);
    append_u16_le(encoded, static_cast<std::uint16_t>(type));
    append_u32_le(encoded, static_cast<std::uint32_t>(payload.size()));
    append(encoded, crypto::sha256(payload));
    append(encoded, payload);
    return encoded;
}

FrameHeaderResult deserialize_frame_header(const crypto::ByteView encoded) {
    if (encoded.size() != frame_header_size) {
        return FrameHeaderResult{ProtocolError::invalid_header_size};
    }
    if (!std::ranges::equal(network_magic, encoded.first(network_magic.size()))) {
        return FrameHeaderResult{ProtocolError::invalid_magic};
    }
    if (read_u16_le(encoded, 4) != wire_version) {
        return FrameHeaderResult{ProtocolError::unsupported_wire_version};
    }
    const std::optional<MessageType> type = decode_message_type(read_u16_le(encoded, 6));
    if (!type.has_value()) {
        return FrameHeaderResult{ProtocolError::unknown_message_type};
    }
    const std::uint32_t payload_length = read_u32_le(encoded, 8);
    if (payload_length > maximum_frame_payload_size) {
        return FrameHeaderResult{ProtocolError::oversized_payload};
    }
    if (payload_length != expected_payload_size(*type)) {
        return FrameHeaderResult{ProtocolError::invalid_payload_size};
    }
    return FrameHeaderResult{FrameHeader{
        *type,
        payload_length,
        read_array<crypto::hash256_size>(encoded, 12),
    }};
}

ProtocolError validate_frame_payload(
    const FrameHeader& header,
    const crypto::ByteView payload
) {
    if (payload.size() != header.payload_length) {
        return ProtocolError::invalid_payload_size;
    }
    return crypto::sha256(payload) == header.payload_checksum
        ? ProtocolError::none
        : ProtocolError::invalid_checksum;
}

crypto::Bytes serialize_hello(const HelloPayload& hello) {
    crypto::Bytes encoded;
    encoded.reserve(hello_payload_size);
    append_u16_le(encoded, hello.minimum_protocol_version);
    append_u16_le(encoded, hello.maximum_protocol_version);
    append_u32_le(encoded, hello.chain_id);
    append_u64_le(encoded, hello.services);
    append(encoded, hello.session_nonce);
    append_u64_le(encoded, hello.tip_height);
    append(encoded, hello.tip_block_id);
    append(encoded, hello.genesis_block_id);
    return encoded;
}

HelloPayloadResult deserialize_hello(const crypto::ByteView encoded) {
    if (encoded.size() != hello_payload_size) {
        return HelloPayloadResult{ProtocolError::invalid_payload_size};
    }
    HelloPayload hello{
        read_u16_le(encoded, 0),
        read_u16_le(encoded, 2),
        read_u32_le(encoded, 4),
        read_u64_le(encoded, 8),
        read_array<16>(encoded, 16),
        read_u64_le(encoded, 32),
        read_array<crypto::hash256_size>(encoded, 40),
        read_array<crypto::hash256_size>(encoded, 72),
    };
    if (hello.minimum_protocol_version == 0 ||
        hello.minimum_protocol_version > hello.maximum_protocol_version) {
        return HelloPayloadResult{ProtocolError::invalid_protocol_range};
    }
    return HelloPayloadResult{std::move(hello)};
}

crypto::Bytes serialize_ping_nonce(const std::uint64_t nonce) {
    crypto::Bytes encoded;
    encoded.reserve(ping_payload_size);
    append_u64_le(encoded, nonce);
    return encoded;
}

std::uint64_t deserialize_ping_nonce(const crypto::ByteView encoded) {
    return encoded.size() == ping_payload_size ? read_u64_le(encoded, 0) : 0;
}

std::string_view protocol_error_message(const ProtocolError error) noexcept {
    switch (error) {
        case ProtocolError::none:
            return "no error";
        case ProtocolError::invalid_header_size:
            return "invalid frame header size";
        case ProtocolError::invalid_magic:
            return "invalid network magic";
        case ProtocolError::unsupported_wire_version:
            return "unsupported wire version";
        case ProtocolError::unknown_message_type:
            return "unknown message type";
        case ProtocolError::oversized_payload:
            return "frame payload exceeds the limit";
        case ProtocolError::invalid_payload_size:
            return "invalid message payload size";
        case ProtocolError::invalid_checksum:
            return "invalid frame payload checksum";
        case ProtocolError::invalid_protocol_range:
            return "invalid protocol version range";
    }
    return "unknown protocol error";
}

}  // namespace bbc::network
