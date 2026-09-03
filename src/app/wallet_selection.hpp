#pragma once

#include <filesystem>
#include <string_view>
#include <variant>

namespace bbc::app::detail {

enum class WalletSelectionError {
    none,
    no_wallet_selected,
    wallet_file_not_found,
    invalid_selection,
    io_error,
};

using WalletSelectionResult =
    std::variant<std::filesystem::path, WalletSelectionError>;

[[nodiscard]] std::filesystem::path default_settings_directory();

[[nodiscard]] WalletSelectionError select_wallet(
    const std::filesystem::path& settings_directory,
    const std::filesystem::path& wallet_path
);

[[nodiscard]] WalletSelectionResult selected_wallet(
    const std::filesystem::path& settings_directory
);

[[nodiscard]] std::string_view wallet_selection_error_message(
    WalletSelectionError error
) noexcept;

}  // namespace bbc::app::detail
