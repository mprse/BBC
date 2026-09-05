#include "bbc/storage/chain_store.hpp"

#include "bbc/crypto/hash.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace bbc::storage {
namespace {

constexpr std::array<crypto::Byte, 4> record_magic{'B', 'B', 'R', 'C'};
constexpr crypto::Byte record_version = 1;
constexpr std::size_t record_header_size = 12;
constexpr std::size_t record_checksum_size = crypto::hash256_size;

template <typename Range>
void append(crypto::Bytes& output, const Range& range) {
    output.insert(output.end(), range.begin(), range.end());
}

void append_u32_le(crypto::Bytes& output, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<crypto::Byte>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_u32_le(const std::array<crypto::Byte, record_header_size>& input) {
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < sizeof(std::uint32_t); ++index) {
        value |= static_cast<std::uint32_t>(input[8 + index]) << (index * 8U);
    }
    return value;
}

std::array<crypto::Byte, sizeof(std::uint64_t)> encode_u64_be(
    const std::uint64_t value
) {
    std::array<crypto::Byte, sizeof(std::uint64_t)> encoded{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const unsigned int shift =
            static_cast<unsigned int>((encoded.size() - index - 1) * 8);
        encoded[index] = static_cast<crypto::Byte>((value >> shift) & 0xFFU);
    }
    return encoded;
}

crypto::Bytes encode_record(const chain::Block& block) {
    const crypto::Bytes payload = block.serialize();
    crypto::Bytes record;
    record.reserve(record_header_size + payload.size() + record_checksum_size);
    append(record, record_magic);
    record.push_back(record_version);
    record.insert(record.end(), 3, 0);
    append_u32_le(record, static_cast<std::uint32_t>(payload.size()));
    append(record, payload);
    append(record, crypto::sha256(payload));
    return record;
}

bool read_exact(std::ifstream& input, crypto::Byte* destination, const std::size_t size) {
    input.read(
        reinterpret_cast<char*>(destination),
        static_cast<std::streamsize>(size)
    );
    return input.good() ||
        (input.eof() && static_cast<std::size_t>(input.gcount()) == size);
}

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

bool execute(sqlite3* database, const char* sql) {
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::string path_as_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

std::unique_ptr<Statement> prepare(sqlite3* database, const char* sql) {
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw_statement, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    return std::make_unique<Statement>(raw_statement);
}

template <typename Range>
bool bind_blob(sqlite3_stmt* statement, const int index, const Range& value) {
    return sqlite3_bind_blob(
               statement,
               index,
               value.data(),
               static_cast<int>(value.size()),
               SQLITE_TRANSIENT
           ) == SQLITE_OK;
}

bool insert_block(
    sqlite3_stmt* statement,
    const chain::Block& block,
    const std::uint64_t cumulative_work,
    const bool active,
    const std::pair<std::uint64_t, std::uint64_t>& location
) {
    if (location.first > static_cast<std::uint64_t>(
                             std::numeric_limits<sqlite3_int64>::max()
                         ) ||
        location.second > static_cast<std::uint64_t>(
                              std::numeric_limits<sqlite3_int64>::max()
                          )) {
        return false;
    }
    const auto height = encode_u64_be(block.height());
    const auto encoded_work = encode_u64_be(cumulative_work);
    if (!bind_blob(statement, 1, block.id()) ||
        !bind_blob(statement, 2, height) ||
        !bind_blob(statement, 3, block.previous_block_hash()) ||
        !bind_blob(statement, 4, encoded_work) ||
        sqlite3_bind_int(statement, 5, active ? 1 : 0) != SQLITE_OK ||
        sqlite3_bind_int64(
            statement,
            6,
            static_cast<sqlite3_int64>(location.first)
        ) != SQLITE_OK ||
        sqlite3_bind_int64(
            statement,
            7,
            static_cast<sqlite3_int64>(location.second)
        ) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_reset(statement) == SQLITE_OK &&
        sqlite3_clear_bindings(statement) == SQLITE_OK;
}

bool insert_account(
    sqlite3_stmt* statement,
    const crypto::Hash256& address,
    const chain::AccountState& account
) {
    const auto balance = encode_u64_be(account.balance);
    const auto next_nonce = encode_u64_be(account.next_nonce);
    if (!bind_blob(statement, 1, address) ||
        !bind_blob(statement, 2, balance) ||
        !bind_blob(statement, 3, next_nonce) ||
        sqlite3_step(statement) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_reset(statement) == SQLITE_OK &&
        sqlite3_clear_bindings(statement) == SQLITE_OK;
}

}  // namespace

ChainStore::ChainStore(
    std::filesystem::path data_directory,
    chain::Blockchain blockchain,
    std::vector<std::pair<std::uint64_t, std::uint64_t>> records
)
    : data_directory_(std::move(data_directory)),
      blockchain_(std::move(blockchain)),
      records_(std::move(records)) {}

ChainStoreResult ChainStore::initialize(
    const std::filesystem::path& data_directory,
    const std::uint32_t chain_id
) {
    const std::filesystem::path chain_directory = data_directory / "chain";
    const std::filesystem::path blocks_file = chain_directory / "blocks.dat";
    const std::filesystem::path database_file = chain_directory / "chain.db";
    std::error_code error;
    const bool blocks_exist = std::filesystem::exists(blocks_file, error);
    if (error) {
        return ChainStoreResult{ChainStoreError::io_error};
    }
    const bool database_exists = std::filesystem::exists(database_file, error);
    if (error) {
        return ChainStoreResult{ChainStoreError::io_error};
    }
    if (blocks_exist || database_exists) {
        return ChainStoreResult{ChainStoreError::already_initialized};
    }
    std::filesystem::create_directories(chain_directory, error);
    if (error) {
        return ChainStoreResult{ChainStoreError::io_error};
    }

    const crypto::Bytes genesis_record = encode_record(chain::genesis_block(chain_id));
    std::ofstream output(blocks_file, std::ios::binary | std::ios::trunc);
    if (!output) {
        return ChainStoreResult{ChainStoreError::io_error};
    }
    output.write(
        reinterpret_cast<const char*>(genesis_record.data()),
        static_cast<std::streamsize>(genesis_record.size())
    );
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(blocks_file, error);
        return ChainStoreResult{ChainStoreError::io_error};
    }
    output.close();

    ChainStoreResult initialized = open(data_directory, chain_id);
    if (!initialized.has_value()) {
        std::filesystem::remove(database_file, error);
        std::filesystem::remove(blocks_file, error);
    }
    return initialized;
}

ChainStoreResult ChainStore::open(
    const std::filesystem::path& data_directory,
    const std::uint32_t chain_id
) {
    const std::filesystem::path blocks_file = data_directory / "chain" / "blocks.dat";
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(blocks_file, file_error)) {
        return ChainStoreResult{
            file_error ? ChainStoreError::io_error
                       : ChainStoreError::not_initialized
        };
    }
    const std::uintmax_t encoded_file_size =
        std::filesystem::file_size(blocks_file, file_error);
    if (file_error || encoded_file_size >
                          std::numeric_limits<std::uint64_t>::max()) {
        return ChainStoreResult{ChainStoreError::io_error};
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(encoded_file_size);

    std::ifstream input(blocks_file, std::ios::binary);
    if (!input) {
        return ChainStoreResult{ChainStoreError::io_error};
    }

    chain::Blockchain blockchain{chain_id};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> records;
    std::uint64_t offset = 0;
    while (offset < file_size) {
        if (file_size - offset < record_header_size + record_checksum_size) {
            return ChainStoreResult{ChainStoreError::invalid_record_size};
        }

        std::array<crypto::Byte, record_header_size> header{};
        if (!read_exact(input, header.data(), header.size())) {
            return ChainStoreResult{ChainStoreError::io_error};
        }
        if (!std::equal(record_magic.begin(), record_magic.end(), header.begin()) ||
            header[4] != record_version || header[5] != 0 ||
            header[6] != 0 || header[7] != 0) {
            return ChainStoreResult{ChainStoreError::invalid_record_header};
        }

        const std::uint32_t payload_size = read_u32_le(header);
        if (payload_size < chain::block_header_size ||
            payload_size > chain::maximum_block_size) {
            return ChainStoreResult{ChainStoreError::invalid_record_size};
        }
        const std::uint64_t record_size = record_header_size +
            static_cast<std::uint64_t>(payload_size) + record_checksum_size;
        if (record_size > file_size - offset) {
            return ChainStoreResult{ChainStoreError::invalid_record_size};
        }

        crypto::Bytes payload(payload_size);
        crypto::Hash256 checksum{};
        if (!read_exact(input, payload.data(), payload.size()) ||
            !read_exact(input, checksum.data(), checksum.size())) {
            return ChainStoreResult{ChainStoreError::io_error};
        }
        if (crypto::sha256(payload) != checksum) {
            return ChainStoreResult{ChainStoreError::invalid_record_checksum};
        }

        chain::BlockResult decoded = chain::deserialize_block(payload);
        if (!decoded.has_value()) {
            return ChainStoreResult{ChainStoreError::invalid_block_record};
        }
        if (records.empty()) {
            if (!decoded.value().is_genesis() ||
                decoded.value().id() != chain::genesis_block(chain_id).id()) {
                return ChainStoreResult{ChainStoreError::invalid_genesis_record};
            }
        } else {
            const chain::AppendResult appended =
                blockchain.append(std::move(decoded).value());
            if (!appended.has_value()) {
                return ChainStoreResult{ChainStoreError::invalid_chain};
            }
        }
        records.emplace_back(offset, record_size);
        offset += record_size;
    }

    if (records.empty()) {
        return ChainStoreResult{ChainStoreError::invalid_genesis_record};
    }

    ChainStore store{data_directory, std::move(blockchain), std::move(records)};
    const ChainStoreError database_error = store.rebuild_database();
    if (database_error != ChainStoreError::none) {
        return ChainStoreResult{database_error};
    }
    return ChainStoreResult{std::move(store)};
}

const std::filesystem::path& ChainStore::data_directory() const noexcept {
    return data_directory_;
}

std::filesystem::path ChainStore::blocks_path() const {
    return data_directory_ / "chain" / "blocks.dat";
}

std::filesystem::path ChainStore::database_path() const {
    return data_directory_ / "chain" / "chain.db";
}

const chain::Blockchain& ChainStore::blockchain() const noexcept {
    return blockchain_;
}

ChainStoreAppendResult ChainStore::append(chain::Block block) {
    chain::Blockchain next_blockchain = blockchain_;
    const chain::AppendResult appended = next_blockchain.append(block);
    if (!appended.has_value()) {
        return {ChainStoreError::none, appended};
    }

    std::error_code file_error;
    const std::uintmax_t encoded_original_size =
        std::filesystem::file_size(blocks_path(), file_error);
    if (file_error || encoded_original_size >
                          std::numeric_limits<std::uint64_t>::max()) {
        return {ChainStoreError::io_error};
    }
    const std::uint64_t original_size =
        static_cast<std::uint64_t>(encoded_original_size);
    const crypto::Bytes record = encode_record(block);

    std::ofstream output(blocks_path(), std::ios::binary | std::ios::app);
    if (!output) {
        return {ChainStoreError::io_error};
    }
    output.write(
        reinterpret_cast<const char*>(record.data()),
        static_cast<std::streamsize>(record.size())
    );
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::resize_file(blocks_path(), original_size, file_error);
        return {ChainStoreError::io_error};
    }
    output.close();

    auto next_records = records_;
    next_records.emplace_back(original_size, record.size());
    ChainStore next_store{
        data_directory_,
        std::move(next_blockchain),
        std::move(next_records),
    };
    const ChainStoreError database_error = next_store.rebuild_database();
    if (database_error != ChainStoreError::none) {
        std::filesystem::resize_file(blocks_path(), original_size, file_error);
        return {
            file_error ? ChainStoreError::io_error : database_error,
        };
    }

    blockchain_ = std::move(next_store.blockchain_);
    records_ = std::move(next_store.records_);
    return {ChainStoreError::none, appended};
}

ChainStoreError ChainStore::rebuild_database() const {
    sqlite3* raw_database = nullptr;
    const std::string encoded_database_path = path_as_utf8(database_path());
    if (sqlite3_open_v2(
            encoded_database_path.c_str(),
            &raw_database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr
        ) != SQLITE_OK) {
        if (raw_database != nullptr) {
            sqlite3_close(raw_database);
        }
        return ChainStoreError::database_error;
    }
    Database database{raw_database};
    if (!execute(database.get(), "PRAGMA journal_mode=DELETE;") ||
        !execute(database.get(), "PRAGMA synchronous=FULL;") ||
        !execute(database.get(), "BEGIN IMMEDIATE;") ||
        !execute(
            database.get(),
            "DROP TABLE IF EXISTS blocks;"
            "DROP TABLE IF EXISTS accounts;"
            "DROP TABLE IF EXISTS metadata;"
            "CREATE TABLE blocks("
            "hash BLOB PRIMARY KEY NOT NULL CHECK(length(hash)=32),"
            "height BLOB NOT NULL CHECK(length(height)=8),"
            "parent_hash BLOB NOT NULL CHECK(length(parent_hash)=32),"
            "cumulative_work BLOB NOT NULL CHECK(length(cumulative_work)=8),"
            "active INTEGER NOT NULL CHECK(active IN(0,1)),"
            "file_offset INTEGER NOT NULL CHECK(file_offset>=0),"
            "record_size INTEGER NOT NULL CHECK(record_size>0)"
            ") WITHOUT ROWID;"
            "CREATE INDEX blocks_by_height ON blocks(height);"
            "CREATE INDEX active_blocks_by_height ON blocks(height) WHERE active=1;"
            "CREATE TABLE accounts("
            "address BLOB PRIMARY KEY NOT NULL CHECK(length(address)=32),"
            "balance BLOB NOT NULL CHECK(length(balance)=8),"
            "next_nonce BLOB NOT NULL CHECK(length(next_nonce)=8)"
            ") WITHOUT ROWID;"
            "CREATE TABLE metadata("
            "id INTEGER PRIMARY KEY CHECK(id=1),"
            "tip_height BLOB NOT NULL CHECK(length(tip_height)=8),"
            "tip_hash BLOB NOT NULL CHECK(length(tip_hash)=32)"
            ");"
            "PRAGMA user_version=2;"
        )) {
        execute(database.get(), "ROLLBACK;");
        return ChainStoreError::database_error;
    }

    std::unique_ptr<Statement> block_statement = prepare(
        database.get(),
        "INSERT INTO blocks(hash,height,parent_hash,cumulative_work,active,"
        "file_offset,record_size) VALUES(?1,?2,?3,?4,?5,?6,?7);"
    );
    std::unique_ptr<Statement> account_statement = prepare(
        database.get(),
        "INSERT INTO accounts(address,balance,next_nonce) VALUES(?1,?2,?3);"
    );
    std::unique_ptr<Statement> metadata_statement = prepare(
        database.get(),
        "INSERT INTO metadata(id,tip_height,tip_hash) VALUES(1,?1,?2);"
    );
    if (!block_statement || !account_statement || !metadata_statement) {
        execute(database.get(), "ROLLBACK;");
        return ChainStoreError::database_error;
    }

    const auto& blocks = blockchain_.stored_blocks();
    if (blocks.size() != records_.size()) {
        execute(database.get(), "ROLLBACK;");
        return ChainStoreError::database_error;
    }
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const std::optional<std::uint64_t> work =
            blockchain_.cumulative_work(blocks[index].id());
        if (!work.has_value() ||
            !insert_block(
                block_statement->get(),
                blocks[index],
                *work,
                blockchain_.is_on_active_chain(blocks[index].id()),
                records_[index]
            )) {
            execute(database.get(), "ROLLBACK;");
            return ChainStoreError::database_error;
        }
    }
    for (const auto& [address, account] : blockchain_.state().accounts()) {
        if (!insert_account(account_statement->get(), address, account)) {
            execute(database.get(), "ROLLBACK;");
            return ChainStoreError::database_error;
        }
    }

    const auto tip_height = encode_u64_be(blockchain_.tip().height());
    if (!bind_blob(metadata_statement->get(), 1, tip_height) ||
        !bind_blob(metadata_statement->get(), 2, blockchain_.tip().id()) ||
        sqlite3_step(metadata_statement->get()) != SQLITE_DONE ||
        !execute(database.get(), "COMMIT;")) {
        execute(database.get(), "ROLLBACK;");
        return ChainStoreError::database_error;
    }
    return ChainStoreError::none;
}

ChainStoreResult::ChainStoreResult(ChainStore store)
    : value_(std::move(store)) {}

ChainStoreResult::ChainStoreResult(const ChainStoreError error) : value_(error) {}

bool ChainStoreResult::has_value() const noexcept {
    return std::holds_alternative<ChainStore>(value_);
}

ChainStore& ChainStoreResult::value() & {
    return std::get<ChainStore>(value_);
}

const ChainStore& ChainStoreResult::value() const& {
    return std::get<ChainStore>(value_);
}

ChainStore&& ChainStoreResult::value() && {
    return std::get<ChainStore>(std::move(value_));
}

ChainStoreError ChainStoreResult::error() const noexcept {
    const auto* error = std::get_if<ChainStoreError>(&value_);
    return error == nullptr ? ChainStoreError::none : *error;
}

std::string_view chain_store_error_message(const ChainStoreError error) noexcept {
    switch (error) {
        case ChainStoreError::none:
            return "no error";
        case ChainStoreError::already_initialized:
            return "chain store is already initialized";
        case ChainStoreError::not_initialized:
            return "chain store is not initialized";
        case ChainStoreError::io_error:
            return "chain store file I/O failed";
        case ChainStoreError::invalid_record_header:
            return "block record header is invalid";
        case ChainStoreError::invalid_record_size:
            return "block record size is invalid or truncated";
        case ChainStoreError::invalid_record_checksum:
            return "block record checksum is invalid";
        case ChainStoreError::invalid_genesis_record:
            return "chain store does not begin with canonical Genesis";
        case ChainStoreError::invalid_block_record:
            return "chain store contains an invalid encoded block";
        case ChainStoreError::invalid_chain:
            return "chain store contains a block that does not extend its chain";
        case ChainStoreError::database_error:
            return "chain index database update failed";
    }
    return "unknown chain store error";
}

}  // namespace bbc::storage
