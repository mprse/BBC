#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace bbc::app::detail {

void print_config_help(std::ostream& output);

int run_config_command(
    std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
);

}  // namespace bbc::app::detail
