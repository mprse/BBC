#include "bbc/storage/mempool_store.hpp"

#include <sqlite3.h>

#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bbc::storage {
namespace {

class Database final {
public:
    explicit Database(sqlite3* handle) : handle_(handle) {}
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database() {
        if (handle_ != nullptr) {
            sqlite3_close(handle_);
        }
    }

    [[nodiscard]] sqlite3* get() const noexcept {
        return handle_;
    }

private:
    sqlite3* handle_;
};

class Statement final {
public:
    explicit Statement(sqlite3_stmt* handle) : handle_(handle) {}
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement() {
        if (handle_ != nullptr) {
            sqlite3_finalize(handle_);
        }
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return handle_;
    }

private:
    sqlite3_stmt* handle_;
};

std::string path_as_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

std::unique_ptr<Database> open_database(const std::filesystem::path& path) {
    sqlite3* raw_database = nullptr;
    if (sqlite3_open(path_as_utf8(path).c_str(), &raw_database) != SQLITE_OK) {
        if (raw_database != nullptr) {
            sqlite3_close(raw_database);
        }
        return nullptr;
    }
    return std::make_unique<Database>(raw_database);
}

std::unique_ptr<Statement> prepare(sqlite3* database, const char* sql) {
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw_statement, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    return std::make_unique<Statement>(raw_statement);
}

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool ensure_schema(sqlite3* database) {
    return execute(
        database,
        "CREATE TABLE IF NOT EXISTS transactions ("
        "arrival_order INTEGER PRIMARY KEY,"
        "transaction_id BLOB NOT NULL UNIQUE,"
        "payload BLOB NOT NULL"
        ")"
    );
}

bool insert_transaction(
    sqlite3_stmt* statement,
    const transaction::SignedTransaction& value
) {
    const crypto::Hash256 id = value.id();
    const crypto::Bytes payload = value.serialize();
    if (sqlite3_bind_blob(
            statement,
            1,
            id.data(),
            static_cast<int>(id.size()),
            SQLITE_TRANSIENT
        ) != SQLITE_OK ||
        sqlite3_bind_blob(
            statement,
            2,
            payload.data(),
            static_cast<int>(payload.size()),
            SQLITE_TRANSIENT
        ) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_reset(statement) == SQLITE_OK &&
        sqlite3_clear_bindings(statement) == SQLITE_OK;
}

MempoolStoreError rewrite(
    const std::filesystem::path& path,
    const mempool::Mempool& value
) {
    std::unique_ptr<Database> database = open_database(path);
    if (!database || !ensure_schema(database->get()) ||
        !execute(database->get(), "BEGIN IMMEDIATE") ||
        !execute(database->get(), "DELETE FROM transactions")) {
        return MempoolStoreError::database_error;
    }
    std::unique_ptr<Statement> insert = prepare(
        database->get(),
        "INSERT INTO transactions(transaction_id, payload) VALUES(?1, ?2)"
    );
    if (!insert) {
        static_cast<void>(execute(database->get(), "ROLLBACK"));
        return MempoolStoreError::database_error;
    }
    for (const transaction::SignedTransaction& pending : value.transactions()) {
        if (!insert_transaction(insert->get(), pending)) {
            static_cast<void>(execute(database->get(), "ROLLBACK"));
            return MempoolStoreError::database_error;
        }
    }
    if (!execute(database->get(), "COMMIT")) {
        static_cast<void>(execute(database->get(), "ROLLBACK"));
        return MempoolStoreError::database_error;
    }
    return MempoolStoreError::none;
}

MempoolStoreError append_transaction(
    const std::filesystem::path& path,
    const transaction::SignedTransaction& value
) {
    std::unique_ptr<Database> database = open_database(path);
    if (!database || !ensure_schema(database->get()) ||
        !execute(database->get(), "BEGIN IMMEDIATE")) {
        return MempoolStoreError::database_error;
    }
    std::unique_ptr<Statement> insert = prepare(
        database->get(),
        "INSERT INTO transactions(transaction_id, payload) VALUES(?1, ?2)"
    );
    if (!insert || !insert_transaction(insert->get(), value) ||
        !execute(database->get(), "COMMIT")) {
        static_cast<void>(execute(database->get(), "ROLLBACK"));
        return MempoolStoreError::database_error;
    }
    return MempoolStoreError::none;
}

}  // namespace

MempoolStore::MempoolStore(
    std::filesystem::path data_directory,
    mempool::Mempool mempool
)
    : data_directory_(std::move(data_directory)),
      mempool_(std::move(mempool)) {}

MempoolStoreResult MempoolStore::open(
    const std::filesystem::path& data_directory,
    const chain::ChainState& chain_state
) {
    const std::filesystem::path directory = data_directory / "mempool";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return MempoolStoreResult{MempoolStoreError::io_error};
    }

    const std::filesystem::path database_path = directory / "mempool.db";
    std::unique_ptr<Database> database = open_database(database_path);
    if (!database || !ensure_schema(database->get())) {
        return MempoolStoreResult{MempoolStoreError::database_error};
    }
    std::unique_ptr<Statement> query = prepare(
        database->get(),
        "SELECT payload FROM transactions ORDER BY arrival_order"
    );
    if (!query) {
        return MempoolStoreResult{MempoolStoreError::database_error};
    }

    mempool::Mempool loaded{chain_state};
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(query->get())) == SQLITE_ROW) {
        const void* payload = sqlite3_column_blob(query->get(), 0);
        const int payload_size = sqlite3_column_bytes(query->get(), 0);
        if (payload == nullptr || payload_size < 0) {
            continue;
        }
        const auto* bytes = static_cast<const crypto::Byte*>(payload);
        transaction::TransactionResult decoded = transaction::deserialize_transaction(
            crypto::ByteView{bytes, static_cast<std::size_t>(payload_size)}
        );
        if (decoded.has_value()) {
            static_cast<void>(loaded.add(std::move(decoded).value()));
        }
    }
    if (step_result != SQLITE_DONE) {
        return MempoolStoreResult{MempoolStoreError::database_error};
    }
    database.reset();

    if (rewrite(database_path, loaded) != MempoolStoreError::none) {
        return MempoolStoreResult{MempoolStoreError::database_error};
    }
    return MempoolStoreResult{MempoolStore{data_directory, std::move(loaded)}};
}

const std::filesystem::path& MempoolStore::data_directory() const noexcept {
    return data_directory_;
}

std::filesystem::path MempoolStore::database_path() const {
    return data_directory_ / "mempool" / "mempool.db";
}

const mempool::Mempool& MempoolStore::mempool() const noexcept {
    return mempool_;
}

MempoolStoreAddResult MempoolStore::add(
    transaction::SignedTransaction transaction
) {
    mempool::Mempool next = mempool_;
    const mempool::MempoolError validation_error = next.add(std::move(transaction));
    if (validation_error != mempool::MempoolError::none) {
        return {MempoolStoreError::none, validation_error};
    }
    const MempoolStoreError storage_error = append_transaction(
        database_path(),
        next.transactions().back()
    );
    if (storage_error != MempoolStoreError::none) {
        return {storage_error, mempool::MempoolError::none};
    }
    mempool_ = std::move(next);
    return {};
}

MempoolStoreRevalidationResult MempoolStore::revalidate(
    const chain::ChainState& chain_state
) {
    mempool::Mempool next = mempool_;
    const std::size_t removed = next.revalidate(chain_state);
    const MempoolStoreError storage_error = rewrite_database(next);
    if (storage_error != MempoolStoreError::none) {
        return {storage_error, 0};
    }
    mempool_ = std::move(next);
    return {MempoolStoreError::none, removed};
}

MempoolStoreError MempoolStore::rewrite_database(
    const mempool::Mempool& value
) const {
    return rewrite(database_path(), value);
}

MempoolStoreResult::MempoolStoreResult(MempoolStore store)
    : value_(std::move(store)) {}

MempoolStoreResult::MempoolStoreResult(const MempoolStoreError error)
    : value_(error) {}

bool MempoolStoreResult::has_value() const noexcept {
    return std::holds_alternative<MempoolStore>(value_);
}

MempoolStore& MempoolStoreResult::value() & {
    return std::get<MempoolStore>(value_);
}

const MempoolStore& MempoolStoreResult::value() const& {
    return std::get<MempoolStore>(value_);
}

MempoolStore&& MempoolStoreResult::value() && {
    return std::get<MempoolStore>(std::move(value_));
}

MempoolStoreError MempoolStoreResult::error() const noexcept {
    const auto* error = std::get_if<MempoolStoreError>(&value_);
    return error == nullptr ? MempoolStoreError::none : *error;
}

std::string_view mempool_store_error_message(
    const MempoolStoreError error
) noexcept {
    switch (error) {
        case MempoolStoreError::none:
            return "no error";
        case MempoolStoreError::io_error:
            return "mempool directory I/O failed";
        case MempoolStoreError::database_error:
            return "mempool database operation failed";
    }
    return "unknown mempool store error";
}

}  // namespace bbc::storage
