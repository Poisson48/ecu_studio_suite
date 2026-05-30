find_package(GTest REQUIRED)

add_executable(test_mpps_sim test_mpps_sim.cpp)
target_include_directories(test_mpps_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/mpps/include
)
target_link_libraries(test_mpps_sim PRIVATE mppslib GTest::gtest_main)
gtest_discover_tests(test_mpps_sim)
