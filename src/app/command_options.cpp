#include "app/command_options.hpp"

#include <algorithm>
#include <ostream>

namespace bbc::app::detail {

std::optional<CommandOptions> parse_options(
    const std::span<const std::string_view> arguments,
    std::ostream& error_output,
    const std::initializer_list<std::string_view> repeatable
) {
    CommandOptions options;
    options.reserve(arguments.size() / 2 + 1);
    for (std::size_t index = 0; index < arguments.size();) {
        const std::string_view name = arguments[index];
        if (!name.starts_with("--")) {
            error_output << "Expected an option but found: " << name << '\n';
            return std::nullopt;
        }
        const bool may_repeat = std::ranges::find(repeatable, name) != repeatable.end();
        if (!may_repeat &&
            std::ranges::any_of(options, [name](const CommandOption& option) {
                return option.name == name;
            })) {
            error_output << "Duplicate option: " << name << '\n';
            return std::nullopt;
        }

        if (name == "--load") {
            options.push_back(CommandOption{name, std::nullopt});
            ++index;
            continue;
        }
        if (index + 1 >= arguments.size()) {
            error_output << "Option requires a value: " << name << '\n';
            return std::nullopt;
        }
        options.push_back(CommandOption{name, arguments[index + 1]});
        index += 2;
    }
    return options;
}

bool validate_options(
    const CommandOptions& options,
    const std::initializer_list<std::string_view> allowed,
    std::ostream& error_output
) {
    for (const CommandOption& option : options) {
        if (std::ranges::find(allowed, option.name) == allowed.end()) {
            error_output << "Unknown option: " << option.name << '\n';
            return false;
        }
    }
    return true;
}

std::optional<std::string_view> required_option(
    const CommandOptions& options,
    const std::string_view name,
    std::ostream& error_output
) {
    const auto option = std::ranges::find_if(
        options,
        [name](const CommandOption& candidate) { return candidate.name == name; }
    );
    if (option == options.end() || !option->value.has_value()) {
        error_output << "Missing required option: " << name << '\n';
        return std::nullopt;
    }
    return option->value;
}

bool has_option(
    const CommandOptions& options,
    const std::string_view name
) noexcept {
    return std::ranges::any_of(options, [name](const CommandOption& option) {
        return option.name == name;
    });
}

std::vector<std::string_view> option_values(
    const CommandOptions& options,
    const std::string_view name
) {
    std::vector<std::string_view> values;
    for (const CommandOption& option : options) {
        if (option.name == name && option.value.has_value()) {
            values.push_back(*option.value);
        }
    }
    return values;
}

}  // namespace bbc::app::detail
