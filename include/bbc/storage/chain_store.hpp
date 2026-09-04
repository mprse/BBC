#pragma once

#include "bbc/chain/blockchain.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bbc::storage {

enum class ChainStoreError {
    none,
    already_initialized,
    not_initialized,
    io_error,
    invalid_record_header,
    invalid_record_size,
    invalid_record_checksum,
    invalid_genesis_record,
    invalid_block_record,
    invalid_chain,
    database_error,
};

struct ChainStoreAppendResult {
    ChainStoreError storage_error = ChainStoreError::none;
    chain::AppendResult chain_result{};

    [[nodiscard]] bool has_value() const noexcept {
        return storage_error == ChainStoreError::none &&
            chain_result.has_value();
    }
};

class ChainStoreResult;

class ChainStore final {
public:
    [[nodiscard]] static ChainStoreResult initialize(
        const std::filesystem::path& data_directory
    );
    [[nodiscard]] static ChainStoreResult open(
        const std::filesystem::path& data_directory
    );

    [[nodiscard]] const std::filesystem::path& data_directory() const noexcept;
    [[nodiscard]] std::filesystem::path blocks_path() const;
    [[nodiscard]] std::filesystem::path database_path() const;
    [[nodiscard]] const chain::Blockchain& blockchain() const noexcept;

    [[nodiscard]] ChainStoreAppendResult append(chain::Block block);

private:
    ChainStore(
        std::filesystem::path data_directory,
        chain::Blockchain blockchain,
        std::vector<std::pair<std::uint64_t, std::uint64_t>> records
    );

    [[nodiscard]] ChainStoreError rebuild_database() const;

    std::filesystem::path data_directory_;
    chain::Blockchain blockchain_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> records_;
};

class ChainStoreResult final {
public:
    explicit ChainStoreResult(ChainStore store);
    explicit ChainStoreResult(ChainStoreError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] ChainStore& value() &;
    [[nodiscard]] const ChainStore& value() const&;
    [[nodiscard]] ChainStore&& value() &&;
    [[nodiscard]] ChainStoreError error() const noexcept;

private:
    std::variant<ChainStore, ChainStoreError> value_;
};

[[nodiscard]] std::string_view chain_store_error_message(
    ChainStoreError error
) noexcept;

}  // namespace bbc::storage
