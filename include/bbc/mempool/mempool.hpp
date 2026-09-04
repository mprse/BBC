#pragma once

#include "bbc/chain/state.hpp"
#include "bbc/transaction/transaction.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace bbc::mempool {

inline constexpr std::size_t maximum_mempool_transactions = 10'000;

enum class MempoolError {
    none,
    full,
    duplicate_transaction,
    account_nonce_conflict,
    unexpected_account_nonce,
    account_nonce_exhausted,
    insufficient_balance,
};

class Mempool final {
public:
    explicit Mempool(chain::ChainState chain_state);

    [[nodiscard]] MempoolError add(transaction::SignedTransaction transaction);
    [[nodiscard]] std::size_t revalidate(chain::ChainState chain_state);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<transaction::SignedTransaction>& transactions()
        const noexcept;
    [[nodiscard]] std::vector<transaction::SignedTransaction> select(
        std::size_t maximum_count
    ) const;

private:
    chain::ChainState chain_state_;
    std::vector<transaction::SignedTransaction> transactions_;
};

[[nodiscard]] std::string_view mempool_error_message(MempoolError error) noexcept;

}  // namespace bbc::mempool
