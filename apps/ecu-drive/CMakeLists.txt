find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 SerialPort Network)
find_package(Qt6 QUIET COMPONENTS Bluetooth)

set(CMAKE_AUTOMOC ON)

set(ECU_DRIVE_SOURCES
    src/main.cpp
    src/drive_controller.cpp
    src/updater.cpp
    src/platform.cpp
)

# Android packaging extras
if(ANDROID)
    list(APPEND ECU_DRIVE_SOURCES
        android/src/org/poisson48/ecudrive/UsbSerialHelper.java
        android/src/org/poisson48/ecudrive/UpdateHelper.java
        android/src/org/poisson48/ecudrive/LoggingService.java
    )
endif()

# Recettes OpenDAMOS embarquées (:/ressources/<ecu>/open_damos.json)
qt_add_resources(ECU_DRIVE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/opendamos_recipes.qrc")
# Logo partagé avec ECU Studio
qt_add_resources(ECU_DRIVE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.qrc")

qt_add_executable(ecu_drive ${ECU_DRIVE_SOURCES})

# Fichiers QML — embarqués dans qrc:/qt/qml/EcuDrive/
qt_add_qml_module(ecu_drive
    URI EcuDrive
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/DrivePage.qml
        qml/SensorsPage.qml
        qml/DiagPage.qml
)

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
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
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

if(ANDROID)
    set_target_properties(ecu_drive PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
    include(FetchContent)
    FetchContent_Declare(android_openssl
        URL https://github.com/KDAB/android_openssl/archive/b71f1470962019bd89534a2919f5925f93bc5779.zip
        URL_HASH SHA256=9277d62ecdb4809801e2c369e0a639c154e0d9137e8d60863b44bf07d16ed5b3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(ecu_drive)
endif()

if(NOT ANDROID)
    install(TARGETS ecu_drive RUNTIME DESTINATION bin)
endif()
