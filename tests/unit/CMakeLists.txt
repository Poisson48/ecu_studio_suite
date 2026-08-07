find_package(GTest REQUIRED)

add_executable(test_mpps_sim test_mpps_sim.cpp)
target_include_directories(test_mpps_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/mpps/include
)
target_link_libraries(test_mpps_sim PRIVATE mppslib GTest::gtest_main)
gtest_discover_tests(test_mpps_sim)

# ── ecu-core module tests ────────────────────────────────────────────────────
# ecu_core links Qt6::Core PUBLIC, so Qt headers/libs come in transitively.
function(ecu_core_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/ecu-core/include
    )
    target_link_libraries(${name} PRIVATE ecu_core GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

ecu_core_test(test_rom_patcher)
ecu_core_test(test_map_finder)
ecu_core_test(test_map_differ)
ecu_core_test(test_ecu_catalog)
ecu_core_test(test_vehicle_templates)
ecu_core_test(test_winols_parser)
ecu_core_test(test_checksum_engine)
ecu_core_test(test_damos_import)
ecu_core_test(test_git_manager)
ecu_core_test(test_edc17_checksum)
ecu_core_test(test_obd2)
ecu_core_test(test_map_sampler)
ecu_core_test(test_tune_validation)

# QCoreApplication requis pour applicationDirPath() (fixtures OpenDAMOS).
add_executable(test_tune_package test_tune_package.cpp)
target_include_directories(test_tune_package PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/ecu-core/include)
target_link_libraries(test_tune_package PRIVATE ecu_core GTest::gtest)
gtest_discover_tests(test_tune_package)

# ECU Drive updater (semver / notes) — indépendant de l'UI
add_executable(test_ecu_drive_updater
    test_ecu_drive_updater.cpp
    ${CMAKE_SOURCE_DIR}/apps/ecu-drive/src/updater.cpp
    ${CMAKE_SOURCE_DIR}/apps/ecu-drive/src/platform.cpp
)
target_include_directories(test_ecu_drive_updater PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/ecu-drive/src)
target_compile_definitions(test_ecu_drive_updater PRIVATE APP_VERSION="1.7.0-test")
target_link_libraries(test_ecu_drive_updater PRIVATE Qt6::Core Qt6::Gui Qt6::Network GTest::gtest)
gtest_discover_tests(test_ecu_drive_updater)

# ── ecu-mcp : serveur MCP JSON-RPC 2.0 ────────────────────────────────────────
# ecu_mcp expose son include PUBLIC et tire nlohmann_json/ecu_core transitivement.
add_executable(test_mcp_server test_mcp_server.cpp)
target_link_libraries(test_mcp_server PRIVATE ecu_mcp GTest::gtest_main)
gtest_discover_tests(test_mcp_server)
