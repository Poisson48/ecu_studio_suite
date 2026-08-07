find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network)
find_package(Qt6 QUIET COMPONENTS Bluetooth)

set(CMAKE_AUTOMOC ON)

set(ECU_DRIVE_SOURCES
    src/main.cpp
    src/drive_window.cpp
    src/updater.cpp
    src/platform.cpp
)

# Android packaging extras
if(ANDROID)
    list(APPEND ECU_DRIVE_SOURCES
        android/src/org/poisson48/ecudrive/UsbSerialHelper.java
        android/src/org/poisson48/ecudrive/UpdateHelper.java
    )
endif()

qt_add_executable(ecu_drive ${ECU_DRIVE_SOURCES})

target_include_directories(ecu_drive PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/libs/ecu-core/include
    ${CMAKE_SOURCE_DIR}/libs/shared/include
    ${CMAKE_SOURCE_DIR}/libs/elm327/include
)

target_compile_definitions(ecu_drive PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ecu_drive PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Network
    ecu_core
    ecu_shared
    elm327
)

if(Qt6Bluetooth_FOUND)
    target_link_libraries(ecu_drive PRIVATE Qt6::Bluetooth)
    target_compile_definitions(ecu_drive PRIVATE ELM_HAVE_BLUETOOTH)
endif()

# Android: C++20 si NDK capricieux sur C++23 pour ce target uniquement
if(ANDROID)
    set_property(TARGET ecu_drive PROPERTY CXX_STANDARD 20)
    set_target_properties(ecu_drive PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()

install(TARGETS ecu_drive RUNTIME DESTINATION bin)
