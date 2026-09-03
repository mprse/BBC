#include "bbc/core/version.hpp"

#ifndef BBC_VERSION
#error "BBC_VERSION must be provided by the build system"
#endif

namespace bbc::core {

std::string_view version() noexcept {
    return BBC_VERSION;
}

}  // namespace bbc::core
