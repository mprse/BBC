cmake_minimum_required(VERSION 3.25)

project(
    BBC
    VERSION 0.1.0
    DESCRIPTION "An educational blockchain project"
    LANGUAGES CXX
)

include(CTest)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

function(bbc_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(
    bbc_core
    src/app/application.cpp
    src/app/block_commands.cpp
    src/app/chain_commands.cpp
    src/app/command_options.cpp
    src/app/mempool_commands.cpp
    src/app/password_reader.cpp
    src/app/transaction_commands.cpp
    src/app/wallet_access.cpp
    src/app/wallet_selection.cpp
    src/chain/block.cpp
    src/chain/block_file.cpp
    src/chain/blockchain.cpp
    src/chain/state.cpp
    src/consensus/proof_of_work.cpp
    src/core/version.cpp
    src/crypto/hash.cpp
    src/crypto/keys.cpp
    src/crypto/secure_memory.cpp
    src/crypto/sodium_runtime.cpp
    src/mempool/mempool.cpp
    src/storage/chain_store.cpp
    src/storage/mempool_store.cpp
    src/transaction/transaction.cpp
    src/transaction/transaction_file.cpp
    src/wallet/address.cpp
    src/wallet/wallet.cpp
    src/wallet/wallet_file.cpp
)
target_include_directories(bbc_core PUBLIC include PRIVATE src)
target_compile_features(bbc_core PUBLIC cxx_std_20)
target_compile_definitions(bbc_core PRIVATE BBC_VERSION="${PROJECT_VERSION}")
target_link_libraries(bbc_core PRIVATE unofficial-sodium::sodium)
target_link_libraries(bbc_core PRIVATE unofficial::sqlite3::sqlite3)
set_target_properties(bbc_core PROPERTIES CXX_EXTENSIONS OFF)
bbc_enable_warnings(bbc_core)

add_executable(bbc src/main.cpp)
target_link_libraries(bbc PRIVATE bbc_core)
set_target_properties(bbc PROPERTIES CXX_EXTENSIONS OFF)
bbc_enable_warnings(bbc)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)

    add_executable(
        bbc_tests
        tests/application_tests.cpp
        tests/block_tests.cpp
        tests/blockchain_tests.cpp
        tests/proof_of_work_tests.cpp
        tests/storage_tests.cpp
        tests/crypto_tests.cpp
        tests/mempool_tests.cpp
        tests/transaction_tests.cpp
        tests/wallet_tests.cpp
    )
    target_link_libraries(bbc_tests PRIVATE bbc_core Catch2::Catch2WithMain)
    set_target_properties(bbc_tests PROPERTIES CXX_EXTENSIONS OFF)
    bbc_enable_warnings(bbc_tests)

    catch_discover_tests(bbc_tests)
endif()
