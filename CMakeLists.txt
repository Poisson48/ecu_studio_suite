cmake_minimum_required(VERSION 3.28)
project(ecu-studio-suite VERSION 1.5.0 LANGUAGES CXX C)

# ── Version ───────────────────────────────────────────────────────────────────
# APP_VERSION (compilé dans le binaire via PROJECT_VERSION) DOIT correspondre au
# tag git de la release. Sinon l'updater propose en boucle une mise à jour déjà
# installée : le manifeste tire sa version du tag (ex. v1.4.1) alors que le binaire
# tirait la sienne de la ligne project() ci-dessus — d'où « toujours 1.3.0 après
# relance ». La CI release passe -DECU_VERSION_OVERRIDE=<tag sans v> pour garder
# binaire et manifeste alignés ; en local on garde la version déclarée ci-dessus.
if(ECU_VERSION_OVERRIDE)
    set(PROJECT_VERSION "${ECU_VERSION_OVERRIDE}")
    message(STATUS "Version surchargée depuis le tag git : ${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
# Build complet de l'app SocketSpy (apps/socketspy) en tant que sous-projet.
# OFF par défaut localement : la GUI SocketSpy requiert Qt6Bluetooth (ELM327),
# souvent absent des machines de dev. Activez-la (ECU_BUILD_SOCKETSPY=ON) sur la
# CI où toutes les dépendances Qt sont présentes pour obtenir le binaire complet
# `socketspy`, qu'ECU Studio peut lancer depuis le panneau « CAN ».
option(ECU_BUILD_SOCKETSPY       "Build complet de l'app SocketSpy"    OFF)
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
# Lua n'est utilisé QUE par le moteur de script de SocketSpy
# (apps/socketspy/scripting). On ne le cherche donc que si on build SocketSpy —
# sinon pkg-config/lua5.4 (paquet Debian) n'a pas à être présent, ce qui rend le
# build portable sous Windows (vcpkg) où ce module pkg-config n'existe pas.
if(ECU_BUILD_SOCKETSPY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA REQUIRED lua5.4)
endif()

# libusb : requis uniquement pour le vrai accès USB (MPPS). En simulation — le cas
# par défaut sous Windows tant que le hardware MPPS n'est pas finalisé — on s'en
# passe (chaque sous-lib gère son propre fallback).
if(NOT ECU_MPPS_SIMULATION)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
else()
    # Mode simulation : pas besoin de libusb
    set(LIBUSB_FOUND FALSE)
    set(LIBUSB_LIBRARIES "")
    set(LIBUSB_INCLUDE_DIRS "")
endif()

find_package(nlohmann_json REQUIRED)
# GTest n'est nécessaire que pour la suite de tests (tests/).
if(ECU_BUILD_TESTS)
    find_package(GTest REQUIRED)
endif()

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
# Flags GCC/Clang uniquement : MSVC ne les comprend pas (-Wpedantic ferait un mur
# d'avertissements D9002). Sous MSVC on reste sur les warnings par défaut.
if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Sous-répertoires ─────────────────────────────────────────────────────────
add_subdirectory(libs/shared)
add_subdirectory(libs/can-core)
add_subdirectory(libs/mpps)
add_subdirectory(libs/ecu-core)
add_subdirectory(libs/ecu-mcp)

# ── SocketSpy ────────────────────────────────────────────────────────────────
# Le submodule SocketSpy est utilisé de deux manières :
#  1. Comme bibliothèque de sources (sidebar, theme, et la lib `cancore` pour le
#     moniteur CAN embarqué d'ECU Studio — voir apps/ecu-studio/CMakeLists.txt).
#  2. Optionnellement, comme app complète buildée à part (ECU_BUILD_SOCKETSPY=ON),
#     qu'ECU Studio peut lancer en sous-processus. Désactivé par défaut car la GUI
#     SocketSpy requiert Qt6Bluetooth, souvent absent en local.
if(ECU_BUILD_SOCKETSPY)
    if(EXISTS "${CMAKE_SOURCE_DIR}/apps/socketspy/CMakeLists.txt")
        # On force les options SocketSpy avant d'inclure le sous-projet :
        # pas de tests (collision de cibles gtest), QML expérimental off.
        set(SOCKETSPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(SOCKETSPY_QML         OFF CACHE BOOL "" FORCE)
        add_subdirectory(apps/socketspy)
    else()
        message(WARNING
            "ECU_BUILD_SOCKETSPY=ON mais apps/socketspy est vide. "
            "Lancez : git submodule update --init apps/socketspy")
    endif()
endif()

if(ECU_BUILD_ECU_STUDIO)
    add_subdirectory(apps/ecu-studio)
endif()
if(ECU_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
