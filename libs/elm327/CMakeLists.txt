find_package(Qt6 REQUIRED COMPONENTS Core SerialPort)
find_package(Qt6 QUIET COMPONENTS Bluetooth)

add_library(elm327 STATIC
    src/Elm327.cpp
    include/elm/Elm327.hpp
)

target_include_directories(elm327
    PUBLIC  include
    PRIVATE ${CMAKE_SOURCE_DIR}/libs/ecu-core/include
)

target_link_libraries(elm327
    PUBLIC  Qt6::Core Qt6::SerialPort
    PRIVATE ecu_core
)

if(Qt6Bluetooth_FOUND)
    target_link_libraries(elm327 PUBLIC Qt6::Bluetooth)
    target_compile_definitions(elm327 PUBLIC ELM_HAVE_BLUETOOTH)
    message(STATUS "elm327 : Qt6::Bluetooth trouvé — RFCOMM activé.")
else()
    message(STATUS "elm327 : Qt6::Bluetooth absent — USB série seulement.")
endif()

set_target_properties(elm327 PROPERTIES AUTOMOC ON)

target_compile_options(elm327 PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>)
