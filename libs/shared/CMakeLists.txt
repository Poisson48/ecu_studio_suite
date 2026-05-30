find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(ecu_shared STATIC
    src/palette.cpp
)

target_include_directories(ecu_shared
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(ecu_shared
    PUBLIC Qt6::Core Qt6::Widgets
)

target_compile_options(ecu_shared PRIVATE -Wall -Wextra -Wpedantic)
