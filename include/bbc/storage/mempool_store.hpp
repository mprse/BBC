#pragma once

#include "bbc/chain/state.hpp"
#include "bbc/mempool/mempool.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <variant>
#include <vector>

namespace bbc::storage {

enum class MempoolStoreError {
    none,
    io_error,
    database_error,
};

struct MempoolStoreAddResult {
    MempoolStoreError storage_error = MempoolStoreError::none;
    mempool::MempoolError mempool_error = mempool::MempoolError::none;

    [[nodiscard]] bool has_value() const noexcept {
        return storage_error == MempoolStoreError::none &&
            mempool_error == mempool::MempoolError::none;
    }
};

struct MempoolStoreRevalidationResult {
    MempoolStoreError error = MempoolStoreError::none;
    std::size_t removed = 0;

    [[nodiscard]] bool has_value() const noexcept {
        return error == MempoolStoreError::none;
    }
};

struct MempoolStoreReorganizationResult {
    MempoolStoreError error = MempoolStoreError::none;
    std::size_t removed = 0;
    std::size_t detached = 0;
    std::size_t restored = 0;
    std::vector<transaction::SignedTransaction> transactions_to_relay;

    [[nodiscard]] bool has_value() const noexcept {
        return error == MempoolStoreError::none;
    }
};

class MempoolStoreResult;

class MempoolStore final {
public:
    [[nodiscard]] static MempoolStoreResult open(
        const std::filesystem::path& data_directory,
        const chain::ChainState& chain_state
    );

    [[nodiscard]] const std::filesystem::path& data_directory() const noexcept;
    [[nodiscard]] std::filesystem::path database_path() const;
    [[nodiscard]] const mempool::Mempool& mempool() const noexcept;

    [[nodiscard]] MempoolStoreAddResult add(
        transaction::SignedTransaction transaction
    );
    [[nodiscard]] MempoolStoreRevalidationResult revalidate(
        const chain::ChainState& chain_state
    );
    [[nodiscard]] MempoolStoreReorganizationResult reconcile_reorganization(
        const chain::ChainState& chain_state,
        const std::vector<chain::Block>& detached_blocks
    );

private:
    MempoolStore(std::filesystem::path data_directory, mempool::Mempool mempool);

    [[nodiscard]] MempoolStoreError rewrite_database(
        const mempool::Mempool& value
    ) const;

    std::filesystem::path data_directory_;
    mempool::Mempool mempool_;
};

class MempoolStoreResult final {
public:
    explicit MempoolStoreResult(MempoolStore store);
    explicit MempoolStoreResult(MempoolStoreError error);

    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] MempoolStore& value() &;
    [[nodiscard]] const MempoolStore& value() const&;
    [[nodiscard]] MempoolStore&& value() &&;
    [[nodiscard]] MempoolStoreError error() const noexcept;

private:
    std::variant<MempoolStore, MempoolStoreError> value_;
};

[[nodiscard]] std::string_view mempool_store_error_message(
    MempoolStoreError error
) noexcept;

}  // namespace bbc::storage
