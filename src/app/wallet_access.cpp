#include "app/wallet_access.hpp"

#include "app/wallet_selection.hpp"
#include "crypto/secure_memory.hpp"

#include <algorithm>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

namespace bbc::app::detail {
namespace {

class SensitiveString final {
public:
    SensitiveString() = default;
    SensitiveString(const SensitiveString&) = delete;
    SensitiveString& operator=(const SensitiveString&) = delete;

    ~SensitiveString() {
        crypto::detail::secure_clear(value_);
    }

    [[nodiscard]] std::string& mutable_value() noexcept {
        return value_;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return value_;
    }

private:
    std::string value_;
};

}  // namespace

std::optional<std::filesystem::path> resolve_wallet_path(
    const CommandOptions& options,
    const std::filesystem::path& settings_directory,
    const std::string_view file_option,
    std::ostream& error_output
) {
    const auto file = std::ranges::find_if(
        options,
        [file_option](const CommandOption& option) { return option.name == file_option; }
    );
    if (file != options.end() && file->value.has_value()) {
        return std::filesystem::path{std::string{*file->value}};
    }

    WalletSelectionResult selection = selected_wallet(settings_directory);
    if (const auto* error = std::get_if<WalletSelectionError>(&selection)) {
        error_output << "Could not resolve wallet: "
                     << wallet_selection_error_message(*error) << '\n';
        return std::nullopt;
    }
    return std::move(std::get<std::filesystem::path>(selection));
}

std::optional<wallet::Wallet> open_wallet(
    const std::filesystem::path& path,
    const PasswordReader& password_reader,
    std::ostream& error_output
) {
    SensitiveString password;
    if (!password_reader ||
        !password_reader("Wallet password: ", password.mutable_value())) {
        error_output << "Could not read a password securely from this terminal.\n";
        return std::nullopt;
    }

    wallet::WalletLoadResult result = wallet::load_wallet(path, password.view());
    if (const auto* error = std::get_if<wallet::WalletFileError>(&result)) {
        error_output << "Could not open wallet: "
                     << wallet::wallet_file_error_message(*error) << '\n';
        return std::nullopt;
    }
    return std::move(std::get<wallet::Wallet>(result));
}

}  // namespace bbc::app::detail
