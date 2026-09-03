#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace bbc::app {

using PasswordReader = std::function<bool(std::string_view prompt, std::string& password)>;

int run(
    std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader = {},
    const std::filesystem::path& settings_directory = {}
);

}  // namespace bbc::app
