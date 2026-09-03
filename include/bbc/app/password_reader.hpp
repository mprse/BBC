#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace bbc::app {

[[nodiscard]] bool read_password_from_terminal(
    std::string_view prompt,
    std::string& password,
    std::ostream& prompt_output
);

}  // namespace bbc::app
