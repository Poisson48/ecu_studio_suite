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

# ── ecu-mcp : serveur MCP JSON-RPC 2.0 ────────────────────────────────────────
# ecu_mcp expose son include PUBLIC et tire nlohmann_json/ecu_core transitivement.
add_executable(test_mcp_server test_mcp_server.cpp)
target_link_libraries(test_mcp_server PRIVATE ecu_mcp GTest::gtest_main)
gtest_discover_tests(test_mcp_server)
