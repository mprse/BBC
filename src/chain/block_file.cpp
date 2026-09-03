#include "bbc/chain/block.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace bbc::chain {

BlockError save_block(const Block& block, const std::filesystem::path& path) {
    std::error_code file_error;
    if (std::filesystem::exists(path, file_error)) {
        return file_error ? BlockError::io_error : BlockError::file_already_exists;
    }
    if (file_error) {
        return BlockError::io_error;
    }

    const crypto::Bytes encoded = block.serialize();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return BlockError::io_error;
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
        return BlockError::io_error;
    }
    return BlockError::none;
}

BlockResult load_block(const std::filesystem::path& path) {
    std::error_code file_error;
    const std::uintmax_t size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        return BlockResult{BlockError::io_error};
    }
    if (size < block_header_size || size > maximum_block_size) {
        return BlockResult{BlockError::invalid_size};
    }

    crypto::Bytes encoded(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(
            reinterpret_cast<char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size())
        )) {
        return BlockResult{BlockError::io_error};
    }
    return deserialize_block(encoded);
}

}  // namespace bbc::chain
