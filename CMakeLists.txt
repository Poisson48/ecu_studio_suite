cmake_minimum_required(VERSION 3.28)
project(ecu-studio-suite VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(ECU_BUILD_SOCKETSPY       "Build SocketSpy"                     ON)
option(ECU_BUILD_ECU_STUDIO      "Build ECU Studio"                    ON)
option(ECU_BUILD_TESTS           "Build tests"                         ON)
option(ECU_MPPS_SIMULATION       "Simulate MPPS (pas de vrai USB)"     OFF)
option(ECU_MPPS_PROTOCOL_LOG     "Logger toutes les trames MPPS"       OFF)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Charts SerialBus SerialPort
    Concurrent Sql Network OpenGL
)
qt_standard_project_setup()

# ── Autres dépendances ───────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA     REQUIRED lua5.4)
pkg_check_modules(LIBGIT2 libgit2)  # optionnel — requis uniquement si GitManager est compilé

if(NOT ECU_MPPS_SIMULATION)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
else()
    # Mode simulation : pas besoin de libusb
    set(LIBUSB_FOUND FALSE)
    set(LIBUSB_LIBRARIES "")
    set(LIBUSB_INCLUDE_DIRS "")
endif()

find_package(nlohmann_json REQUIRED)
find_package(GTest         REQUIRED)

# ── Platform ─────────────────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(ECU_PLATFORM_WINDOWS)
else()
    add_compile_definitions(ECU_PLATFORM_LINUX ECU_SOCKETCAN_AVAILABLE)
endif()

if(ECU_MPPS_PROTOCOL_LOG)
    add_compile_definitions(ECU_MPPS_PROTOCOL_LOG)
endif()

if(ECU_MPPS_SIMULATION)
    add_compile_definitions(ECU_MPPS_SIMULATION)
endif()

# ── Compile flags ────────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Sous-répertoires ─────────────────────────────────────────────────────────
add_subdirectory(libs/shared)
add_subdirectory(libs/can-core)
add_subdirectory(libs/mpps)
add_subdirectory(libs/ecu-core)

# SocketSpy est un submodule utilisé comme bibliothèque de sources (sidebar, theme)
# On ne le build pas comme sous-projet — ses sources sont incluses directement dans ecu_studio
if(ECU_BUILD_ECU_STUDIO)
    add_subdirectory(apps/ecu-studio)
endif()
if(ECU_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
