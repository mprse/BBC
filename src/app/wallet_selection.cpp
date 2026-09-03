#include "app/wallet_selection.hpp"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

namespace bbc::app::detail {
namespace {

constexpr std::string_view selected_wallet_file_name = "selected-wallet";
constexpr std::uintmax_t maximum_selection_size = 32U * 1024U;

std::filesystem::path environment_path(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    const std::unique_ptr<char, decltype(&std::free)> owner{value, &std::free};
    return value[0] != '\0' ? std::filesystem::path{value} : std::filesystem::path{};
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? std::filesystem::path{value}
                                                 : std::filesystem::path{};
#endif
}

std::filesystem::path selection_file(const std::filesystem::path& settings_directory) {
    return settings_directory / selected_wallet_file_name;
}

std::string encode_path(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

std::filesystem::path decode_path(const std::string& encoded) {
    const std::u8string utf8{
        reinterpret_cast<const char8_t*>(encoded.data()),
        encoded.size(),
    };
    return std::filesystem::path{utf8};
}

}  // namespace

std::filesystem::path default_settings_directory() {
    const std::filesystem::path override_directory = environment_path("BBC_CONFIG_HOME");
    if (!override_directory.empty()) {
        return override_directory;
    }

#ifdef _WIN32
    const std::filesystem::path local_app_data = environment_path("LOCALAPPDATA");
    if (!local_app_data.empty()) {
        return local_app_data / "BBC";
    }
    const std::filesystem::path user_profile = environment_path("USERPROFILE");
    return user_profile.empty() ? std::filesystem::path{}
                                : user_profile / "AppData" / "Local" / "BBC";
#elif defined(__APPLE__)
    const std::filesystem::path home = environment_path("HOME");
    return home.empty() ? std::filesystem::path{}
                        : home / "Library" / "Application Support" / "BBC";
#else
    const std::filesystem::path xdg_config_home = environment_path("XDG_CONFIG_HOME");
    if (!xdg_config_home.empty()) {
        return xdg_config_home / "bbc";
    }
    const std::filesystem::path home = environment_path("HOME");
    return home.empty() ? std::filesystem::path{} : home / ".config" / "bbc";
#endif
}

WalletSelectionError select_wallet(
    const std::filesystem::path& settings_directory,
    const std::filesystem::path& wallet_path
) {
    if (settings_directory.empty() || wallet_path.empty()) {
        return WalletSelectionError::invalid_selection;
    }

    std::error_code error;
    const bool is_wallet_file = std::filesystem::is_regular_file(wallet_path, error);
    if (error) {
        return WalletSelectionError::io_error;
    }
    if (!is_wallet_file) {
        return WalletSelectionError::wallet_file_not_found;
    }

    const std::filesystem::path absolute_wallet_path =
        std::filesystem::absolute(wallet_path, error).lexically_normal();
    if (error) {
        return WalletSelectionError::io_error;
    }

    std::filesystem::create_directories(settings_directory, error);
    if (error) {
        return WalletSelectionError::io_error;
    }

    const std::string encoded_path = encode_path(absolute_wallet_path);
    if (encoded_path.empty() || encoded_path.size() > maximum_selection_size ||
        encoded_path.find('\0') != std::string::npos ||
        encoded_path.find('\n') != std::string::npos ||
        encoded_path.find('\r') != std::string::npos) {
        return WalletSelectionError::invalid_selection;
    }

    std::ofstream output(selection_file(settings_directory), std::ios::binary | std::ios::trunc);
    if (!output) {
        return WalletSelectionError::io_error;
    }
    output.write(encoded_path.data(), static_cast<std::streamsize>(encoded_path.size()));
    output.put('\n');
    output.flush();
    return output.good() ? WalletSelectionError::none : WalletSelectionError::io_error;
}

WalletSelectionResult selected_wallet(
    const std::filesystem::path& settings_directory
) {
    if (settings_directory.empty()) {
        return WalletSelectionError::invalid_selection;
    }

    const std::filesystem::path path = selection_file(settings_directory);
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return WalletSelectionError::no_wallet_selected;
    }
    if (error) {
        return WalletSelectionError::io_error;
    }
    if (size == 0 || size > maximum_selection_size) {
        return WalletSelectionError::invalid_selection;
    }

    std::string encoded_path(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(encoded_path.data(), static_cast<std::streamsize>(encoded_path.size()))) {
        return WalletSelectionError::io_error;
    }
    while (!encoded_path.empty() &&
           (encoded_path.back() == '\n' || encoded_path.back() == '\r')) {
        encoded_path.pop_back();
    }
    if (encoded_path.empty() || encoded_path.find('\0') != std::string::npos ||
        encoded_path.find('\n') != std::string::npos ||
        encoded_path.find('\r') != std::string::npos) {
        return WalletSelectionError::invalid_selection;
    }

    const std::filesystem::path wallet_path = decode_path(encoded_path);
    const bool is_wallet_file = std::filesystem::is_regular_file(wallet_path, error);
    if (error) {
        return WalletSelectionError::io_error;
    }
    if (!is_wallet_file) {
        return WalletSelectionError::wallet_file_not_found;
    }
    return wallet_path;
}

std::string_view wallet_selection_error_message(
    const WalletSelectionError error
) noexcept {
    switch (error) {
        case WalletSelectionError::none:
            return "no error";
        case WalletSelectionError::no_wallet_selected:
            return "no wallet is selected; use 'bbc wallet select --file <path>'";
        case WalletSelectionError::wallet_file_not_found:
            return "the selected wallet file does not exist";
        case WalletSelectionError::invalid_selection:
            return "the selected-wallet setting is invalid";
        case WalletSelectionError::io_error:
            return "selected-wallet settings could not be read or written";
    }
    return "unknown wallet selection error";
}

}  // namespace bbc::app::detail
