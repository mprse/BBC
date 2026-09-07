cmake_minimum_required(VERSION 3.25)

project(
    BBC
    VERSION 0.1.0
    DESCRIPTION "An educational blockchain project"
    LANGUAGES CXX
)

include(GNUInstallDirs)

include(CTest)
find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
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
    src/app/config_commands.cpp
    src/app/mempool_commands.cpp
    src/app/node_commands.cpp
    src/app/rpc_commands.cpp
    src/app/password_reader.cpp
    src/app/transaction_commands.cpp
    src/app/wallet_access.cpp
    src/app/wallet_selection.cpp
    src/chain/block.cpp
    src/chain/block_file.cpp
    src/chain/blockchain.cpp
    src/chain/state.cpp
    src/consensus/proof_of_work.cpp
    src/config/application_config.cpp
    src/core/version.cpp
    src/core/network.cpp
    src/crypto/hash.cpp
    src/crypto/keys.cpp
    src/crypto/secure_memory.cpp
    src/crypto/sodium_runtime.cpp
    src/mempool/mempool.cpp
    src/network/protocol.cpp
    src/network/peer_network.cpp
    src/node/control_server.cpp
    src/node/scenario_actor_config.cpp
    src/rpc/token.cpp
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
if(WIN32)
    target_compile_definitions(bbc_core PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(bbc_core PRIVATE unofficial-sodium::sodium)
target_link_libraries(bbc_core PRIVATE unofficial::sqlite3::sqlite3)
target_link_libraries(bbc_core PRIVATE asio::asio)
target_link_libraries(bbc_core PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(bbc_core PROPERTIES CXX_EXTENSIONS OFF)
bbc_enable_warnings(bbc_core)

add_executable(bbc src/main.cpp)
target_link_libraries(bbc PRIVATE bbc_core)
set_target_properties(bbc PROPERTIES CXX_EXTENSIONS OFF)
bbc_enable_warnings(bbc)

set(BBC_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/bbc")

install(TARGETS bbc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
    FILES README.md LICENSE
    DESTINATION "${BBC_INSTALL_DOCDIR}"
)
install(
    DIRECTORY docs/
    DESTINATION "${BBC_INSTALL_DOCDIR}/docs"
    FILES_MATCHING PATTERN "*.md"
)
install(
    FILES examples/ec2-node.json
    DESTINATION "${CMAKE_INSTALL_DATADIR}/bbc/examples"
)
install(
    FILES packaging/systemd/bbc.service
    DESTINATION "${CMAKE_INSTALL_DATADIR}/bbc/systemd"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(
        CPACK_PACKAGE_FILE_NAME
        "bbc-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}"
    )
    set(CPACK_PACKAGE_VENDOR "BBC educational project")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bi-Bi-Coin educational node")
    set(CPACK_VERBATIM_VARIABLES YES)
    include(CPack)
endif()

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    include(Catch)

    add_executable(
        bbc_tests
        tests/application_tests.cpp
        tests/application_config_tests.cpp
        tests/block_tests.cpp
        tests/blockchain_tests.cpp
    tests/proof_of_work_tests.cpp
    tests/rpc_tests.cpp
        tests/storage_tests.cpp
        tests/crypto_tests.cpp
        tests/mempool_tests.cpp
        tests/network_tests.cpp
        tests/node_tests.cpp
        tests/transaction_tests.cpp
        tests/wallet_tests.cpp
    )
    target_link_libraries(bbc_tests PRIVATE bbc_core Catch2::Catch2WithMain asio::asio)
    if(WIN32)
        target_compile_definitions(bbc_tests PRIVATE _WIN32_WINNT=0x0A00)
    endif()
    set_target_properties(bbc_tests PROPERTIES CXX_EXTENSIONS OFF)
    bbc_enable_warnings(bbc_tests)

    catch_discover_tests(bbc_tests)

    add_test(
        NAME BBC.local-network-scenario-smoke
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/two-actor-smoke.json"
            --executable "$<TARGET_FILE:bbc>"
            --no-ui
    )
    set_tests_properties(BBC.local-network-scenario-smoke PROPERTIES TIMEOUT 30)

    add_test(
        NAME BBC.local-mining-race-regtest
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/mining-race.json"
            --executable "$<TARGET_FILE:bbc>"
            --regtest
            --no-ui
    )
    set_tests_properties(BBC.local-mining-race-regtest PROPERTIES TIMEOUT 30)

    add_test(
        NAME BBC.local-transaction-propagation-regtest
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/transaction-propagation.json"
            --executable "$<TARGET_FILE:bbc>"
            --regtest
            --no-ui
    )
    set_tests_properties(
        BBC.local-transaction-propagation-regtest
        PROPERTIES TIMEOUT 30
    )

    add_test(
        NAME BBC.local-transaction-confirmation-regtest
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/transaction-confirmation.json"
            --executable "$<TARGET_FILE:bbc>"
            --regtest
            --no-ui
    )
    set_tests_properties(
        BBC.local-transaction-confirmation-regtest
        PROPERTIES TIMEOUT 30
    )

    add_test(
        NAME BBC.local-late-join-sync-regtest
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/late-join-sync.json"
            --executable "$<TARGET_FILE:bbc>"
            --regtest
            --no-ui
    )
    set_tests_properties(
        BBC.local-late-join-sync-regtest
        PROPERTIES TIMEOUT 30
    )

    add_test(
        NAME BBC.local-fork-reorg-regtest
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/scenario.py"
            "${CMAKE_SOURCE_DIR}/scenarios/fork-reorg.json"
            --executable "$<TARGET_FILE:bbc>"
            --regtest
            --no-ui
    )
    set_tests_properties(
        BBC.local-fork-reorg-regtest
        PROPERTIES TIMEOUT 45
    )
endif()
