#pragma once

#include "bbc/crypto/types.hpp"

#include <span>
#include <string>
#include <string_view>

namespace bbc::crypto {

[[nodiscard]] Hash256 sha256(ByteView input);
[[nodiscard]] std::string to_upper_hex(ByteView input);
[[nodiscard]] bool decode_hex(std::string_view input, std::span<Byte> output) noexcept;

}  // namespace bbc::crypto
