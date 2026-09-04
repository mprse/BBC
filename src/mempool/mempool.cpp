#include "bbc/mempool/mempool.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>

namespace bbc::mempool {
namespace {

struct SenderQueue {
    std::vector<std::size_t> transaction_indices;
    std::size_t next = 0;
};

}  // namespace

Mempool::Mempool(chain::ChainState chain_state)
    : chain_state_(std::move(chain_state)) {}

MempoolError Mempool::add(transaction::SignedTransaction transaction) {
    if (transactions_.size() >= maximum_mempool_transactions) {
        return MempoolError::full;
    }

    const crypto::Hash256 transaction_id = transaction.id();
    const wallet::Address sender = transaction.sender();
    const chain::AccountState account = chain_state_.account(sender);
    std::uint64_t reserved = 0;
    std::uint64_t pending_count = 0;

    for (const transaction::SignedTransaction& pending : transactions_) {
        if (pending.id() == transaction_id) {
            return MempoolError::duplicate_transaction;
        }
        if (pending.sender() != sender) {
            continue;
        }
        if (pending.account_nonce() == transaction.account_nonce()) {
            return MempoolError::account_nonce_conflict;
        }
        const std::uint64_t debit = pending.amount() + pending.fee();
        if (reserved > std::numeric_limits<std::uint64_t>::max() - debit) {
            return MempoolError::insufficient_balance;
        }
        reserved += debit;
        ++pending_count;
    }

    if (account.next_nonce == std::numeric_limits<std::uint64_t>::max() ||
        pending_count > std::numeric_limits<std::uint64_t>::max() - account.next_nonce) {
        return MempoolError::account_nonce_exhausted;
    }
    if (transaction.account_nonce() != account.next_nonce + pending_count) {
        return MempoolError::unexpected_account_nonce;
    }

    const std::uint64_t debit = transaction.amount() + transaction.fee();
    if (reserved > account.balance || debit > account.balance - reserved) {
        return MempoolError::insufficient_balance;
    }

    transactions_.push_back(std::move(transaction));
    return MempoolError::none;
}

std::size_t Mempool::revalidate(chain::ChainState chain_state) {
    std::vector<transaction::SignedTransaction> previous = std::move(transactions_);
    chain_state_ = std::move(chain_state);
    transactions_.clear();
    transactions_.reserve(previous.size());
    for (transaction::SignedTransaction& pending : previous) {
        static_cast<void>(add(std::move(pending)));
    }
    return previous.size() - transactions_.size();
}

std::size_t Mempool::size() const noexcept {
    return transactions_.size();
}

const std::vector<transaction::SignedTransaction>& Mempool::transactions()
    const noexcept {
    return transactions_;
}

std::vector<transaction::SignedTransaction> Mempool::select(
    const std::size_t maximum_count
) const {
    std::map<crypto::Hash256, SenderQueue> queues;
    for (std::size_t index = 0; index < transactions_.size(); ++index) {
        queues[transactions_[index].sender().hash()].transaction_indices.push_back(index);
    }

    std::vector<transaction::SignedTransaction> selected;
    selected.reserve(std::min(maximum_count, transactions_.size()));
    while (selected.size() < maximum_count) {
        SenderQueue* best_queue = nullptr;
        const transaction::SignedTransaction* best_transaction = nullptr;
        for (auto& [sender, queue] : queues) {
            static_cast<void>(sender);
            if (queue.next >= queue.transaction_indices.size()) {
                continue;
            }
            const transaction::SignedTransaction& candidate =
                transactions_[queue.transaction_indices[queue.next]];
            if (best_transaction == nullptr ||
                candidate.fee() > best_transaction->fee() ||
                (candidate.fee() == best_transaction->fee() &&
                 candidate.id() < best_transaction->id())) {
                best_queue = &queue;
                best_transaction = &candidate;
            }
        }
        if (best_transaction == nullptr) {
            break;
        }
        selected.push_back(*best_transaction);
        ++best_queue->next;
    }
    return selected;
}

std::string_view mempool_error_message(const MempoolError error) noexcept {
    switch (error) {
        case MempoolError::none:
            return "no error";
        case MempoolError::full:
            return "mempool transaction limit reached";
        case MempoolError::duplicate_transaction:
            return "transaction is already in the mempool";
        case MempoolError::account_nonce_conflict:
            return "another transaction uses the same sender account nonce";
        case MempoolError::unexpected_account_nonce:
            return "transaction account nonce is not the next pending value";
        case MempoolError::account_nonce_exhausted:
            return "sender account nonce is exhausted";
        case MempoolError::insufficient_balance:
            return "confirmed sender balance is insufficient for pending debits";
    }
    return "unknown mempool error";
}

}  // namespace bbc::mempool
