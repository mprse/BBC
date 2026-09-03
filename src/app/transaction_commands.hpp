#pragma once

#include "bbc/app/application.hpp"

#include <filesystem>
#include <iosfwd>
#include <span>
#include <string_view>

namespace bbc::app::detail {

void print_transaction_help(std::ostream& output);

[[nodiscard]] int run_transaction_command(
    std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output,
    const PasswordReader& password_reader,
    const std::filesystem::path& settings_directory
);

}  // namespace bbc::app::detail
