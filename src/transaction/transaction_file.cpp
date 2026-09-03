#include "bbc/transaction/transaction.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace bbc::transaction {

TransactionError save_transaction(
    const SignedTransaction& transaction,
    const std::filesystem::path& path
) {
    std::error_code file_error;
    if (std::filesystem::exists(path, file_error)) {
        return file_error ? TransactionError::io_error
                          : TransactionError::file_already_exists;
    }
    if (file_error) {
        return TransactionError::io_error;
    }

    const crypto::Bytes encoded = transaction.serialize();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return TransactionError::io_error;
    }
    output.write(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size())
    );
    output.flush();
    const bool write_succeeded = output.good();
    output.close();

    if (!write_succeeded) {
        std::filesystem::remove(path, file_error);
        return TransactionError::io_error;
    }
    return TransactionError::none;
}

TransactionResult load_transaction(const std::filesystem::path& path) {
    std::error_code file_error;
    const std::uintmax_t size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        return TransactionResult{TransactionError::io_error};
    }
    if (size != signed_transaction_size) {
        return TransactionResult{TransactionError::invalid_size};
    }

    crypto::Bytes encoded(signed_transaction_size);
    std::ifstream input(path, std::ios::binary);
    if (!input.read(
            reinterpret_cast<char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size())
        )) {
        return TransactionResult{TransactionError::io_error};
    }
    return deserialize_transaction(encoded);
}

}  // namespace bbc::transaction
