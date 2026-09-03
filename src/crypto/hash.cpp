#include "bbc/crypto/hash.hpp"

#include "crypto/sodium_runtime.hpp"

#include <sodium.h>

namespace bbc::crypto {
namespace {

int decode_nibble(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

}  // namespace

static_assert(hash256_size == crypto_hash_sha256_BYTES);

Hash256 sha256(const ByteView input) {
    detail::ensure_sodium_initialized();

    Hash256 hash{};
    crypto_hash_sha256(hash.data(), input.data(), input.size());
    return hash;
}

std::string to_upper_hex(const ByteView input) {
    constexpr char alphabet[] = "0123456789ABCDEF";

    std::string result;
    result.reserve(input.size() * 2);
    for (const Byte value : input) {
        result.push_back(alphabet[value >> 4U]);
        result.push_back(alphabet[value & 0x0FU]);
    }
    return result;
}

bool decode_hex(const std::string_view input, const std::span<Byte> output) noexcept {
    if (input.size() != output.size() * 2) {
        return false;
    }

    for (std::size_t index = 0; index < output.size(); ++index) {
        const int high = decode_nibble(input[index * 2]);
        const int low = decode_nibble(input[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] = static_cast<Byte>((high << 4) | low);
    }
    return true;
}

}  // namespace bbc::crypto
