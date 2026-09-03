#include "bbc/crypto/hash.hpp"
#include "bbc/crypto/keys.hpp"
#include "bbc/wallet/address.hpp"
#include "bbc/wallet/wallet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string_view>

namespace {

bbc::crypto::Bytes bytes(const std::string_view text) {
    bbc::crypto::Bytes result;
    result.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(result), [](const char value) {
        return static_cast<bbc::crypto::Byte>(value);
    });
    return result;
}

}  // namespace

TEST_CASE("SHA-256 and uppercase hexadecimal encoding are deterministic", "[crypto]") {
    std::array<bbc::crypto::Byte, bbc::crypto::public_key_size> input{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<bbc::crypto::Byte>(index);
    }

    const bbc::crypto::Hash256 hash = bbc::crypto::sha256(input);

    CHECK(
        bbc::crypto::to_upper_hex(hash) ==
        "630DCD2966C4336691125448BBB25B4FF412A49C732DB2C8ABC1B8581BD710DD"
    );
}

TEST_CASE("hexadecimal decoding accepts either letter case and rejects invalid input", "[crypto]") {
    std::array<bbc::crypto::Byte, 3> decoded{};

    REQUIRE(bbc::crypto::decode_hex("0aB1ff", decoded));
    CHECK(decoded == std::array<bbc::crypto::Byte, 3>{0x0A, 0xB1, 0xFF});
    CHECK_FALSE(bbc::crypto::decode_hex("0AB1F", decoded));
    CHECK_FALSE(bbc::crypto::decode_hex("0AB1FG", decoded));
}

TEST_CASE("BBC address is derived from the SHA-256 public-key hash", "[crypto][wallet]") {
    bbc::crypto::PublicKey public_key{};
    for (std::size_t index = 0; index < public_key.size(); ++index) {
        public_key[index] = static_cast<bbc::crypto::Byte>(index);
    }

    const bbc::wallet::Address address =
        bbc::wallet::Address::from_public_key(public_key);

    CHECK(
        address.value() ==
        "BBC_630DCD2966C4336691125448BBB25B4FF412A49C732DB2C8ABC1B8581BD710DD"
    );
}

TEST_CASE("BBC address parsing is strict and canonical", "[crypto][wallet]") {
    constexpr std::string_view lowercase_address =
        "BBC_630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd";
    const std::optional<bbc::wallet::Address> parsed =
        bbc::wallet::Address::parse(lowercase_address);

    REQUIRE(parsed.has_value());
    CHECK(
        parsed->value() ==
        "BBC_630DCD2966C4336691125448BBB25B4FF412A49C732DB2C8ABC1B8581BD710DD"
    );
    CHECK_FALSE(bbc::wallet::Address::parse("bbc_630D").has_value());
    CHECK_FALSE(bbc::wallet::Address::parse("BBC_not-hexadecimal").has_value());
}

TEST_CASE("Ed25519 signatures authenticate the wallet and exact message", "[crypto][wallet]") {
    bbc::wallet::Wallet wallet_a = bbc::wallet::Wallet::create();
    bbc::wallet::Wallet wallet_b = bbc::wallet::Wallet::create();
    const bbc::crypto::Bytes message = bytes("A sends 10 BBC to B");
    const bbc::crypto::Bytes modified_message = bytes("A sends 100 BBC to C");

    const bbc::crypto::Signature signature = wallet_a.sign(message);

    CHECK(bbc::crypto::verify_signature(message, signature, wallet_a.public_key()));
    CHECK_FALSE(
        bbc::crypto::verify_signature(modified_message, signature, wallet_a.public_key())
    );
    CHECK_FALSE(bbc::crypto::verify_signature(message, signature, wallet_b.public_key()));
    CHECK(wallet_a.address() != wallet_b.address());
}

TEST_CASE("malformed Ed25519 private keys are rejected", "[crypto]") {
    bbc::crypto::Bytes malformed_private_key(bbc::crypto::private_key_size, 0);

    CHECK_FALSE(
        bbc::crypto::KeyPair::from_private_key(malformed_private_key).has_value()
    );
}
