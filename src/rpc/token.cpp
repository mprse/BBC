#include "bbc/rpc/token.hpp"

#include "bbc/crypto/hash.hpp"
#include "crypto/sodium_runtime.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace bbc::rpc {
namespace {

constexpr std::size_t token_byte_size = 32;
constexpr std::size_t token_text_size = token_byte_size * 2;
constexpr std::size_t maximum_token_file_size = 256;

bool valid_token(const std::string_view value) {
    return value.size() == token_text_size &&
        std::ranges::all_of(value, [](const unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
        });
}

}  // namespace

TokenResult::TokenResult(std::string token) : value_(std::move(token)) {}

TokenResult::TokenResult(const TokenError error) : value_(error) {}

bool TokenResult::has_value() const noexcept {
    return std::holds_alternative<std::string>(value_);
}

const std::string& TokenResult::value() const& {
    return std::get<std::string>(value_);
}

std::string&& TokenResult::value() && {
    return std::get<std::string>(std::move(value_));
}

TokenError TokenResult::error() const noexcept {
    const auto* error = std::get_if<TokenError>(&value_);
    return error == nullptr ? TokenError::none : *error;
}

TokenResult load_token(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > maximum_token_file_size) {
        return TokenResult{error ? TokenError::io_error : TokenError::invalid_token};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return TokenResult{TokenError::io_error};
    }
    std::string token{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (input.bad()) {
        return TokenResult{TokenError::io_error};
    }
    if (!token.empty() && token.back() == '\n') {
        token.pop_back();
        if (!token.empty() && token.back() == '\r') {
            token.pop_back();
        }
    }
    if (!valid_token(token)) {
        return TokenResult{TokenError::invalid_token};
    }
    return TokenResult{std::move(token)};
}

TokenResult load_or_create_token(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return error ? TokenResult{TokenError::io_error} : load_token(path);
    }
    if (error) {
        return TokenResult{TokenError::io_error};
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return TokenResult{TokenError::io_error};
        }
    }

    crypto::detail::ensure_sodium_initialized();
    std::array<crypto::Byte, token_byte_size> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::string token = crypto::to_upper_hex(bytes);

    std::array<crypto::Byte, 8> suffix_bytes{};
    randombytes_buf(suffix_bytes.data(), suffix_bytes.size());
    const std::filesystem::path temporary{
        path.string() + ".tmp-" + crypto::to_upper_hex(suffix_bytes)
    };
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return TokenResult{TokenError::io_error};
        }
        output << token << '\n';
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return TokenResult{TokenError::io_error};
        }
    }

    std::filesystem::permissions(
        temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        error
    );
    if (error) {
        std::filesystem::remove(temporary, error);
        return TokenResult{TokenError::io_error};
    }

    std::filesystem::create_hard_link(temporary, path, error);
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    if (error) {
        std::error_code exists_error;
        if (std::filesystem::exists(path, exists_error) && !exists_error) {
            return load_token(path);
        }
        return TokenResult{TokenError::io_error};
    }
    return TokenResult{std::move(token)};
}

std::string_view token_error_message(const TokenError error) noexcept {
    switch (error) {
        case TokenError::none:
            return "no error";
        case TokenError::io_error:
            return "RPC token file I/O failed";
        case TokenError::invalid_token:
            return "RPC token file does not contain one 64-character hexadecimal token";
    }
    return "unknown RPC token error";
}

}  // namespace bbc::rpc
