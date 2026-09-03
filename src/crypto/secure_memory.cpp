#include "crypto/secure_memory.hpp"

#include <sodium.h>

namespace bbc::crypto::detail {

void secure_clear(std::string& value) noexcept {
    if (!value.empty()) {
        sodium_memzero(value.data(), value.size());
        value.clear();
    }
}

}  // namespace bbc::crypto::detail
