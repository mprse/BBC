#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace bbc::crypto {

using Byte = unsigned char;
using Bytes = std::vector<Byte>;
using ByteView = std::span<const Byte>;

inline constexpr std::size_t public_key_size = 32;
inline constexpr std::size_t private_key_size = 64;
inline constexpr std::size_t signature_size = 64;
inline constexpr std::size_t hash256_size = 32;

using PublicKey = std::array<Byte, public_key_size>;
using Signature = std::array<Byte, signature_size>;
using Hash256 = std::array<Byte, hash256_size>;

}  // namespace bbc::crypto
