#pragma once

#include <string>

namespace bbc::crypto::detail {

void secure_clear(std::string& value) noexcept;

}  // namespace bbc::crypto::detail
