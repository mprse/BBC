#pragma once

#include <initializer_list>
#include <iosfwd>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bbc::app::detail {

struct CommandOption {
    std::string_view name;
    std::optional<std::string_view> value;
};

using CommandOptions = std::vector<CommandOption>;

[[nodiscard]] std::optional<CommandOptions> parse_options(
    std::span<const std::string_view> arguments,
    std::ostream& error_output,
    std::initializer_list<std::string_view> repeatable = {}
);

[[nodiscard]] bool validate_options(
    const CommandOptions& options,
    std::initializer_list<std::string_view> allowed,
    std::ostream& error_output
);

[[nodiscard]] std::optional<std::string_view> required_option(
    const CommandOptions& options,
    std::string_view name,
    std::ostream& error_output
);

[[nodiscard]] bool has_option(
    const CommandOptions& options,
    std::string_view name
) noexcept;

[[nodiscard]] std::vector<std::string_view> option_values(
    const CommandOptions& options,
    std::string_view name
);

}  // namespace bbc::app::detail
