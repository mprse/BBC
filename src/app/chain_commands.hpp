#pragma once

#include "bbc/wallet/address.hpp"

#include <iosfwd>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bbc::app::detail {

void print_chain_help(std::ostream& output);

[[nodiscard]] int show_chain_balance(
    const wallet::Address& address,
    const std::vector<std::string_view>& block_files,
    const std::optional<std::filesystem::path>& data_directory,
    std::ostream& output,
    std::ostream& error_output
);

[[nodiscard]] int run_chain_command(
    std::span<const std::string_view> arguments,
    std::ostream& output,
    std::ostream& error_output
);

}  // namespace bbc::app::detail
