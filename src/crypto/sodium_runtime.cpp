#include "crypto/sodium_runtime.hpp"

#include <sodium.h>

#include <stdexcept>

namespace bbc::crypto::detail {

void ensure_sodium_initialized() {
    static const bool initialized = [] {
        if (sodium_init() < 0) {
            throw std::runtime_error("Could not initialize libsodium");
        }
        return true;
    }();

    static_cast<void>(initialized);
}

}  // namespace bbc::crypto::detail
