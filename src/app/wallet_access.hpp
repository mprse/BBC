#pragma once

#include "app/command_options.hpp"
#include "bbc/app/application.hpp"
#include "bbc/wallet/wallet.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace bbc::app::detail {

[[nodiscard]] std::optional<std::filesystem::path> resolve_wallet_path(
    const CommandOptions& options,
    const std::filesystem::path& settings_directory,
    std::string_view file_option,
    std::ostream& error_output
);

[[nodiscard]] std::optional<wallet::Wallet> open_wallet(
    const std::filesystem::path& path,
    const PasswordReader& password_reader,
    std::ostream& error_output
);

}  // namespace bbc::app::detail
