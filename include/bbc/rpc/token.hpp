#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace bbc::rpc {

enum class TokenError {
    none,
    io_error,
    invalid_token,
};

class TokenResult final {
public:
    explicit TokenResult(std::string token);
    explicit TokenResult(TokenError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] const std::string& value() const&;
    [[nodiscard]] std::string&& value() &&;
    [[nodiscard]] TokenError error() const noexcept;

private:
    std::variant<std::string, TokenError> value_;
};

[[nodiscard]] TokenResult load_token(const std::filesystem::path& path);
[[nodiscard]] TokenResult load_or_create_token(const std::filesystem::path& path);
[[nodiscard]] std::string_view token_error_message(TokenError error) noexcept;

}  // namespace bbc::rpc
